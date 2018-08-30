//--------------------------------------------------------------------------------------------------
// Copyright (c) YugaByte, Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License"); you may not use this file except
// in compliance with the License.  You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software distributed under the License
// is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express
// or implied.  See the License for the specific language governing permissions and limitations
// under the License.
//
//--------------------------------------------------------------------------------------------------

#include "yb/yql/cql/ql/exec/executor.h"
#include "yb/yql/cql/ql/ql_processor.h"
#include "yb/client/client.h"
#include "yb/client/callbacks.h"
#include "yb/client/yb_op.h"
#include "yb/common/common.pb.h"
#include "yb/common/ql_protocol_util.h"
#include "yb/common/wire_protocol.h"
#include "yb/rpc/thread_pool.h"
#include "yb/util/decimal.h"
#include "yb/util/logging.h"
#include "yb/util/thread_restrictions.h"
#include "yb/util/trace.h"

namespace yb {
namespace ql {

using std::string;
using std::shared_ptr;
using namespace std::placeholders;

using client::YBColumnSpec;
using client::YBSchema;
using client::YBSchemaBuilder;
using client::YBTable;
using client::YBTableCreator;
using client::YBTableAlterer;
using client::YBTableType;
using client::YBTableName;
using client::YBSessionPtr;
using client::YBOperation;
using client::YBqlOp;
using client::YBqlReadOp;
using client::YBqlWriteOp;
using client::YBqlOpPtr;
using client::YBqlReadOpPtr;
using client::YBqlWriteOpPtr;
using strings::Substitute;

#define RETURN_STMT_NOT_OK(s) do {                                         \
    auto&& _s = (s);                                                       \
    if (PREDICT_FALSE(!_s.ok())) return StatementExecuted(MoveStatus(_s)); \
  } while (false)

//--------------------------------------------------------------------------------------------------

Executor::Executor(QLEnv *ql_env, Rescheduler rescheduler, const QLMetrics* ql_metrics)
    : ql_env_(ql_env),
      rescheduler_(std::move(rescheduler)),
      session_(ql_env_->NewSession()),
      ql_metrics_(ql_metrics) {
}

Executor::~Executor() {
}

//--------------------------------------------------------------------------------------------------

void Executor::ExecuteAsync(const ParseTree& parse_tree, const StatementParameters& params,
                            StatementExecutedCallback cb) {
  DCHECK(cb_.is_null()) << "Another execution is in progress.";
  cb_ = std::move(cb);
  session_->SetReadPoint(client::Restart::kFalse);
  RETURN_STMT_NOT_OK(Execute(parse_tree, params));
  FlushAsync();
}

void Executor::ExecuteAsync(const StatementBatch& batch, StatementExecutedCallback cb) {
  DCHECK(cb_.is_null()) << "Another execution is in progress.";
  cb_ = std::move(cb);
  session_->SetReadPoint(client::Restart::kFalse);

  // Table for DML batches, where all statements must modify the same table.
  client::YBTablePtr dml_batch_table;

  // Verify the statements in the batch.
  for (const auto& pair : batch) {
    const ParseTree& parse_tree = pair.first;
    const TreeNode* tnode = parse_tree.root().get();
    if (tnode != nullptr) {
      switch (tnode->opcode()) {
        case TreeNodeOpcode::kPTInsertStmt: FALLTHROUGH_INTENDED;
        case TreeNodeOpcode::kPTUpdateStmt: FALLTHROUGH_INTENDED;
        case TreeNodeOpcode::kPTDeleteStmt: {
          const auto *stmt = static_cast<const PTDmlStmt *>(tnode);
          if (stmt->if_clause() != nullptr && !stmt->returns_status()) {
            return StatementExecuted(
                ErrorStatus(ErrorCode::CQL_STATEMENT_INVALID,
                            "batch execution of conditional DML statement without RETURNS STATUS "
                            "AS ROW clause is not supported yet"));
          }

          if (stmt->ModifiesMultipleRows()) {
            return StatementExecuted(
                ErrorStatus(ErrorCode::CQL_STATEMENT_INVALID,
                            "batch execution with DML statements modifying multiple rows is not "
                            "supported yet"));
          }

          if (!returns_status_batch_opt_) {
            returns_status_batch_opt_ = stmt->returns_status();
          } else if (stmt->returns_status() != *returns_status_batch_opt_) {
            return StatementExecuted(
                ErrorStatus(ErrorCode::CQL_STATEMENT_INVALID,
                            "batch execution mixing statements with and without RETURNS STATUS "
                            "AS ROW is not supported"));
          }

          if (*returns_status_batch_opt_) {
            if (dml_batch_table == nullptr) {
              dml_batch_table = stmt->table();
            } else if (dml_batch_table->id() != stmt->table()->id()) {
              return StatementExecuted(
                  ErrorStatus(ErrorCode::CQL_STATEMENT_INVALID,
                              "batch execution with RETURNS STATUS statements cannot span multiple "
                              "tables"));
            }
          }

          break;
        }
        default:
          return StatementExecuted(
              ErrorStatus(ErrorCode::CQL_STATEMENT_INVALID,
                          "batch execution supports INSERT, UPDATE and DELETE statements only "
                          "currently"));
          break;
      }
    }
  }

  for (const auto& pair : batch) {
    const ParseTree& parse_tree = pair.first;
    const StatementParameters& params = pair.second;
    RETURN_STMT_NOT_OK(Execute(parse_tree, params));
  }

  FlushAsync();
}

//--------------------------------------------------------------------------------------------------

Status Executor::Execute(const ParseTree& parse_tree, const StatementParameters& params) {
  // Prepare execution context and execute the parse tree's root node.
  exec_contexts_.emplace_back(parse_tree, params);
  exec_context_ = &exec_contexts_.back();
  return ProcessStatementStatus(parse_tree, ExecTreeNode(parse_tree.root().get()));
}

//--------------------------------------------------------------------------------------------------

Status Executor::ExecTreeNode(const TreeNode *tnode) {
  if (tnode == nullptr) {
    return Status::OK();
  }
  TnodeContext* tnode_context = nullptr;
  if (tnode->opcode() != TreeNodeOpcode::kPTListNode) {
    tnode_context = exec_context_->AddTnode(tnode);
    if (tnode->IsDml() && static_cast<const PTDmlStmt *>(tnode)->RequiresTransaction()) {
      exec_context_->StartTransaction(SNAPSHOT_ISOLATION, ql_env_);
    }
  }
  switch (tnode->opcode()) {
    case TreeNodeOpcode::kPTListNode:
      return ExecPTNode(static_cast<const PTListNode *>(tnode));

    case TreeNodeOpcode::kPTCreateTable: FALLTHROUGH_INTENDED;
    case TreeNodeOpcode::kPTCreateIndex:
      return ExecPTNode(static_cast<const PTCreateTable *>(tnode));

    case TreeNodeOpcode::kPTAlterTable:
      return ExecPTNode(static_cast<const PTAlterTable *>(tnode));

    case TreeNodeOpcode::kPTCreateType:
      return ExecPTNode(static_cast<const PTCreateType *>(tnode));

    case TreeNodeOpcode::kPTCreateRole:
      return ExecPTNode(static_cast<const PTCreateRole *>(tnode));

    case TreeNodeOpcode::kPTAlterRole:
      return ExecPTNode(static_cast<const PTAlterRole *>(tnode));

    case TreeNodeOpcode::kPTGrantRevokeRole:
      return ExecPTNode(static_cast<const PTGrantRevokeRole *>(tnode));

    case TreeNodeOpcode::kPTDropStmt:
      return ExecPTNode(static_cast<const PTDropStmt *>(tnode));

    case TreeNodeOpcode::kPTGrantRevokePermission:
      return ExecPTNode(static_cast<const PTGrantRevokePermission *>(tnode));

    case TreeNodeOpcode::kPTSelectStmt:
      return ExecPTNode(static_cast<const PTSelectStmt *>(tnode), tnode_context);

    case TreeNodeOpcode::kPTInsertStmt:
      return ExecPTNode(static_cast<const PTInsertStmt *>(tnode), tnode_context);

    case TreeNodeOpcode::kPTDeleteStmt:
      return ExecPTNode(static_cast<const PTDeleteStmt *>(tnode), tnode_context);

    case TreeNodeOpcode::kPTUpdateStmt:
      return ExecPTNode(static_cast<const PTUpdateStmt *>(tnode), tnode_context);

    case TreeNodeOpcode::kPTStartTransaction:
      return ExecPTNode(static_cast<const PTStartTransaction *>(tnode));

    case TreeNodeOpcode::kPTCommit:
      return ExecPTNode(static_cast<const PTCommit *>(tnode));

    case TreeNodeOpcode::kPTTruncateStmt:
      return ExecPTNode(static_cast<const PTTruncateStmt *>(tnode));

    case TreeNodeOpcode::kPTCreateKeyspace:
      return ExecPTNode(static_cast<const PTCreateKeyspace *>(tnode));

    case TreeNodeOpcode::kPTUseKeyspace:
      return ExecPTNode(static_cast<const PTUseKeyspace *>(tnode));

    default:
      return exec_context_->Error(tnode, ErrorCode::FEATURE_NOT_SUPPORTED);
  }
}

Status Executor::ExecPTNode(const PTCreateRole *tnode) {
  const Status s = ql_env_->CreateRole(tnode->role_name(), tnode->salted_hash(), tnode->login(),
                                       tnode->superuser());
  if (PREDICT_FALSE(!s.ok())) {
    ErrorCode error_code = ErrorCode::SERVER_ERROR;
    if (s.IsAlreadyPresent()) {
      error_code = ErrorCode::DUPLICATE_ROLE;
    }

    if (tnode->create_if_not_exists() && error_code == ErrorCode::DUPLICATE_ROLE) {
      return Status::OK();
    }
    // TODO (Bristy) : Set result_ properly.
    return exec_context_->Error(tnode, s, error_code);
  }

  return Status::OK();
}

//--------------------------------------------------------------------------------------------------

Status Executor::ExecPTNode(const PTAlterRole *tnode) {
  const Status s = ql_env_->AlterRole(tnode->role_name(), tnode->salted_hash(), tnode->login(),
                                      tnode->superuser());
  if (PREDICT_FALSE(!s.ok())) {
    ErrorCode error_code = ErrorCode::ROLE_NOT_FOUND;
    return exec_context_->Error(tnode, s, error_code);
  }

  return Status::OK();
}

//--------------------------------------------------------------------------------------------------

Status Executor::ExecPTNode(const PTGrantRevokeRole* tnode) {
  const Status s = ql_env_->GrantRevokeRole(tnode->statement_type(), tnode->granted_role_name(),
                                            tnode->recipient_role_name());
  if (PREDICT_FALSE(!s.ok())) {
    ErrorCode error_code = ErrorCode::SERVER_ERROR;
    if (s.IsInvalidArgument()) {
      error_code = ErrorCode::INVALID_REQUEST;
    }
    if (s.IsNotFound()) {
      error_code = ErrorCode::ROLE_NOT_FOUND;
    }
    // TODO (Bristy) : Set result_ properly.
    return exec_context_->Error(tnode, s, error_code);
  }
  return Status::OK();
}

//--------------------------------------------------------------------------------------------------

Status Executor::ExecPTNode(const PTListNode *tnode) {
  for (TreeNode::SharedPtr dml : tnode->node_list()) {
    RETURN_NOT_OK(ExecTreeNode(dml.get()));
  }
  return Status::OK();
}

//--------------------------------------------------------------------------------------------------

Status Executor::ExecPTNode(const PTCreateType *tnode) {
  YBTableName yb_name = tnode->yb_type_name();

  const std::string& type_name = yb_name.table_name();
  std::string keyspace_name = yb_name.namespace_name();

  std::vector<std::string> field_names;
  std::vector<std::shared_ptr<QLType>> field_types;

  for (const PTTypeField::SharedPtr field : tnode->fields()->node_list()) {
    field_names.emplace_back(field->yb_name());
    field_types.push_back(field->ql_type());
  }

  Status s = ql_env_->CreateUDType(keyspace_name, type_name, field_names, field_types);
  if (PREDICT_FALSE(!s.ok())) {
    ErrorCode error_code = ErrorCode::SERVER_ERROR;
    if (s.IsAlreadyPresent()) {
      error_code = ErrorCode::DUPLICATE_TYPE;
    } else if (s.IsNotFound()) {
      error_code = ErrorCode::KEYSPACE_NOT_FOUND;
    }

    if (tnode->create_if_not_exists() && error_code == ErrorCode::DUPLICATE_TYPE) {
      return Status::OK();
    }

    return exec_context_->Error(tnode->type_name(), s, error_code);
  }

  result_ = std::make_shared<SchemaChangeResult>("CREATED", "TYPE", keyspace_name, type_name);
  return Status::OK();
}

//--------------------------------------------------------------------------------------------------

Status Executor::ExecPTNode(const PTCreateTable *tnode) {
  YBTableName table_name = tnode->yb_table_name();

  if (table_name.is_system() && client::FLAGS_yb_system_namespace_readonly) {
    return exec_context_->Error(tnode->table_name(), ErrorCode::SYSTEM_NAMESPACE_READONLY);
  }

  // Setting up columns.
  Status s;
  YBSchema schema;
  YBSchemaBuilder b;

  const MCList<PTColumnDefinition *>& hash_columns = tnode->hash_columns();
  for (const auto& column : hash_columns) {
    if (column->sorting_type() != ColumnSchema::SortingType::kNotSpecified) {
      return exec_context_->Error(tnode->columns().front(), s, ErrorCode::INVALID_TABLE_DEFINITION);
    }
    b.AddColumn(column->yb_name())->Type(column->ql_type())
        ->HashPrimaryKey()
        ->Order(column->order());
  }
  const MCList<PTColumnDefinition *>& primary_columns = tnode->primary_columns();
  for (const auto& column : primary_columns) {
    b.AddColumn(column->yb_name())->Type(column->ql_type())
        ->PrimaryKey()
        ->Order(column->order())
        ->SetSortingType(column->sorting_type());
  }
  const MCList<PTColumnDefinition *>& columns = tnode->columns();
  for (const auto& column : columns) {
    if (column->sorting_type() != ColumnSchema::SortingType::kNotSpecified) {
      return exec_context_->Error(tnode->columns().front(), s, ErrorCode::INVALID_TABLE_DEFINITION);
    }
    YBColumnSpec *column_spec = b.AddColumn(column->yb_name())->Type(column->ql_type())
                                ->Nullable()
                                ->Order(column->order());
    if (column->is_static()) {
      column_spec->StaticColumn();
    }
    if (column->is_counter()) {
      column_spec->Counter();
    }
  }

  TableProperties table_properties;
  s = tnode->ToTableProperties(&table_properties);
  if (!s.ok()) {
    return exec_context_->Error(tnode->columns().front(), s, ErrorCode::INVALID_TABLE_DEFINITION);
  }
  b.SetTableProperties(table_properties);

  s = b.Build(&schema);
  if (PREDICT_FALSE(!s.ok())) {
    return exec_context_->Error(tnode->columns().front(), s, ErrorCode::INVALID_TABLE_DEFINITION);
  }

  // Create table.
  shared_ptr<YBTableCreator> table_creator(ql_env_->NewTableCreator());
  table_creator->table_name(table_name)
      .table_type(YBTableType::YQL_TABLE_TYPE)
      .schema(&schema);
  if (tnode->opcode() == TreeNodeOpcode::kPTCreateIndex) {
    const PTCreateIndex *index_node = static_cast<const PTCreateIndex*>(tnode);
    table_creator->indexed_table_id(index_node->indexed_table_id());
    table_creator->is_local_index(index_node->is_local());
    table_creator->is_unique_index(index_node->is_unique());
  }
  s = table_creator->Create();
  if (PREDICT_FALSE(!s.ok())) {
    ErrorCode error_code = ErrorCode::SERVER_ERROR;
    if (s.IsAlreadyPresent()) {
      error_code = ErrorCode::DUPLICATE_TABLE;
    } else if (s.IsNotFound()) {
      error_code = tnode->opcode() == TreeNodeOpcode::kPTCreateIndex
                   ? ErrorCode::TABLE_NOT_FOUND
                   : ErrorCode::KEYSPACE_NOT_FOUND;
    } else if (s.IsInvalidArgument()) {
      error_code = ErrorCode::INVALID_TABLE_DEFINITION;
    }

    if (tnode->create_if_not_exists() && error_code == ErrorCode::DUPLICATE_TABLE) {
      return Status::OK();
    }

    return exec_context_->Error(tnode->table_name(), s, error_code);
  }

  if (tnode->opcode() == TreeNodeOpcode::kPTCreateIndex) {
    const YBTableName indexed_table_name =
        static_cast<const PTCreateIndex*>(tnode)->indexed_table_name();
    result_ = std::make_shared<SchemaChangeResult>(
        "UPDATED", "TABLE", indexed_table_name.namespace_name(), indexed_table_name.table_name());
    ql_env_->RemoveCachedTableDesc(indexed_table_name);
  } else {
    result_ = std::make_shared<SchemaChangeResult>(
        "CREATED", "TABLE", table_name.namespace_name(), table_name.table_name());
  }
  return Status::OK();
}

//--------------------------------------------------------------------------------------------------

Status Executor::ExecPTNode(const PTAlterTable *tnode) {
  YBTableName table_name = tnode->yb_table_name();

  shared_ptr<YBTableAlterer> table_alterer(ql_env_->NewTableAlterer(table_name));

  for (const auto& mod_column : tnode->mod_columns()) {
    switch (mod_column->mod_type()) {
      case ALTER_ADD:
        table_alterer->AddColumn(mod_column->new_name()->data())
            ->Type(mod_column->ql_type());
        break;
      case ALTER_DROP:
        table_alterer->DropColumn(mod_column->old_name()->last_name().data());
        break;
      case ALTER_RENAME:
        table_alterer->AlterColumn(mod_column->old_name()->last_name().data())
            ->RenameTo(mod_column->new_name()->c_str());
        break;
      case ALTER_TYPE:
        // Not yet supported by AlterTableRequestPB.
        return exec_context_->Error(tnode, ErrorCode::FEATURE_NOT_YET_IMPLEMENTED);
    }
  }

  if (!tnode->mod_props().empty()) {
    TableProperties table_properties;
    Status s = tnode->ToTableProperties(&table_properties);
    if(PREDICT_FALSE(!s.ok())) {
      return exec_context_->Error(tnode, s, ErrorCode::INVALID_ARGUMENTS);
    }

    table_alterer->SetTableProperties(table_properties);
  }

  Status s = table_alterer->Alter();
  if (PREDICT_FALSE(!s.ok())) {
    return exec_context_->Error(tnode, s, ErrorCode::EXEC_ERROR);
  }

  result_ = std::make_shared<SchemaChangeResult>(
      "UPDATED", "TABLE", table_name.namespace_name(), table_name.table_name());
  ql_env_->RemoveCachedTableDesc(table_name);
  return Status::OK();
}

//--------------------------------------------------------------------------------------------------

Status Executor::ExecPTNode(const PTDropStmt *tnode) {
  Status s;
  ErrorCode error_not_found = ErrorCode::SERVER_ERROR;

  switch (tnode->drop_type()) {
    case OBJECT_TABLE: {
      // Drop the table.
      const YBTableName table_name = tnode->yb_table_name();
      s = ql_env_->DeleteTable(table_name);
      error_not_found = ErrorCode::TABLE_NOT_FOUND;
      result_ = std::make_shared<SchemaChangeResult>(
          "DROPPED", "TABLE", table_name.namespace_name(), table_name.table_name());
      ql_env_->RemoveCachedTableDesc(table_name);
      break;
    }

    case OBJECT_INDEX: {
      // Drop the index.
      const YBTableName table_name = tnode->yb_table_name();
      YBTableName indexed_table_name;
      s = ql_env_->DeleteIndexTable(table_name, &indexed_table_name);
      error_not_found = ErrorCode::TABLE_NOT_FOUND;
      result_ = std::make_shared<SchemaChangeResult>(
          "UPDATED", "TABLE", indexed_table_name.namespace_name(), indexed_table_name.table_name());
      ql_env_->RemoveCachedTableDesc(indexed_table_name);
      break;
    }

    case OBJECT_SCHEMA: {
      // Drop the keyspace.
      const string keyspace_name(tnode->name()->last_name().c_str());
      s = ql_env_->DeleteKeyspace(keyspace_name);
      error_not_found = ErrorCode::KEYSPACE_NOT_FOUND;
      result_ = std::make_shared<SchemaChangeResult>("DROPPED", "KEYSPACE", keyspace_name);
      break;
    }

    case OBJECT_TYPE: {
      // Drop the type.
      const string type_name(tnode->name()->last_name().c_str());
      const string namespace_name(tnode->name()->first_name().c_str());
      s = ql_env_->DeleteUDType(namespace_name, type_name);
      error_not_found = ErrorCode::TYPE_NOT_FOUND;
      result_ = std::make_shared<SchemaChangeResult>("DROPPED", "TYPE", namespace_name, type_name);
      ql_env_->RemoveCachedUDType(namespace_name, type_name);
      break;
    }

    case OBJECT_ROLE: {
      // Drop the role.
      const string role_name(tnode->name()->QLName());
      s = ql_env_->DeleteRole(role_name);
      error_not_found = ErrorCode::ROLE_NOT_FOUND;
      // TODO (Bristy) : Set result_ properly.
      break;
    }

    default:
      return exec_context_->Error(tnode->name(), ErrorCode::FEATURE_NOT_SUPPORTED);
  }

  if (PREDICT_FALSE(!s.ok())) {
    ErrorCode error_code = ErrorCode::SERVER_ERROR;

    if (s.IsNotFound()) {
      // Ignore not found error for a DROP IF EXISTS statement.
      if (tnode->drop_if_exists()) {
        return Status::OK();
      }

      error_code = error_not_found;
    }

    return exec_context_->Error(tnode->name(), s, error_code);
  }

  return Status::OK();
}

Status Executor::ExecPTNode(const PTGrantRevokePermission* tnode) {
  const string role_name = tnode->role_name()->QLName();
  const string canonical_resource = tnode->canonical_resource();
  const char* resource_name = tnode->resource_name();
  const char* namespace_name = tnode->namespace_name();
  ResourceType resource_type = tnode->resource_type();
  PermissionType permission = tnode->permission();
  const auto statement_type = tnode->statement_type();

  Status s = ql_env_->GrantRevokePermission(statement_type, permission, resource_type,
                                            canonical_resource, resource_name, namespace_name,
                                            role_name);

  if (PREDICT_FALSE(!s.ok())) {
    ErrorCode error_code = ErrorCode::SERVER_ERROR;
    if (s.IsInvalidArgument()) {
      error_code = ErrorCode::INVALID_ARGUMENTS;
    }
    if (s.IsNotFound()) {
      error_code = ErrorCode::RESOURCE_NOT_FOUND;
    }
    return exec_context_->Error(tnode, s, error_code);
  }
  // TODO (Bristy) : Return proper result.
  return Status::OK();
}

Status Executor::GetOffsetOrLimit(
    const PTSelectStmt* tnode,
    const std::function<PTExpr::SharedPtr(const PTSelectStmt* tnode)>& get_val,
    const string& clause_type,
    int32_t* value) {
  QLExpressionPB expr_pb;
  Status s = (PTExprToPB(get_val(tnode), &expr_pb));
  if (PREDICT_FALSE(!s.ok())) {
    return exec_context_->Error(get_val(tnode), s, ErrorCode::INVALID_ARGUMENTS);
  }

  if (expr_pb.has_value() && IsNull(expr_pb.value())) {
    return exec_context_->Error(get_val(tnode),
                                Substitute("$0 value cannot be null.", clause_type).c_str(),
                                ErrorCode::INVALID_ARGUMENTS);
  }

  // This should be ensured by checks before getting here.
  DCHECK(expr_pb.has_value() && expr_pb.value().has_int32_value())
      << "Integer constant expected for " + clause_type + " clause";

  if (expr_pb.value().int32_value() < 0) {
    return exec_context_->Error(get_val(tnode),
                                Substitute("$0 value cannot be negative.", clause_type).c_str(),
                                ErrorCode::INVALID_ARGUMENTS);
  }
  *value = expr_pb.value().int32_value();
  return Status::OK();
}

//--------------------------------------------------------------------------------------------------

Status Executor::ExecPTNode(const PTSelectStmt *tnode, TnodeContext* tnode_context) {
  const shared_ptr<client::YBTable>& table = tnode->table();
  if (table == nullptr) {
    // If this is a system table but the table does not exist, it is okay. Just return OK with void
    // result.
    return tnode->is_system() ? Status::OK()
                              : exec_context_->Error(tnode, ErrorCode::TABLE_NOT_FOUND);
  }

  const StatementParameters& params = exec_context_->params();
  // If there is a table id in the statement parameter's paging state, this is a continuation of
  // a prior SELECT statement. Verify that the same table still exists.
  const bool continue_select = !params.table_id().empty();
  if (continue_select && params.table_id() != table->id()) {
    return exec_context_->Error(tnode, "Table no longer exists.", ErrorCode::TABLE_NOT_FOUND);
  }

  // Create the read request.
  YBqlReadOpPtr select_op(table->NewQLSelect());
  QLReadRequestPB *req = select_op->mutable_request();
  // Where clause - Hash, range, and regular columns.

  req->set_is_aggregate(tnode->is_aggregate());

  Result<uint64_t> max_rows_estimate = WhereClauseToPB(req, tnode->key_where_ops(),
                                                       tnode->where_ops(),
                                                       tnode->subscripted_col_where_ops(),
                                                       tnode->json_col_where_ops(),
                                                       tnode->partition_key_ops(),
                                                       tnode->func_ops(),
                                                       tnode_context);
  if (PREDICT_FALSE(!max_rows_estimate)) {
    return exec_context_->Error(tnode, max_rows_estimate.status(), ErrorCode::INVALID_ARGUMENTS);
  }

  // If where clause restrictions guarantee no rows could match, return empty result immediately.
  if (*max_rows_estimate == 0 && !tnode->is_aggregate()) {
    QLRowBlock empty_row_block(tnode->table()->InternalSchema(), {});
    faststring buffer;
    empty_row_block.Serialize(select_op->request().client(), &buffer);
    *select_op->mutable_rows_data() = buffer.ToString();
    result_ = std::make_shared<RowsResult>(select_op.get());
    return Status::OK();
  }

  req->set_is_forward_scan(tnode->is_forward_scan());

  // Specify selected list by adding the expressions to selected_exprs in read request.
  QLRSRowDescPB *rsrow_desc_pb = req->mutable_rsrow_desc();
  for (const auto& expr : tnode->selected_exprs()) {
    if (expr->opcode() == TreeNodeOpcode::kPTAllColumns) {
      const Status s = PTExprToPB(static_cast<const PTAllColumns*>(expr.get()), req);
      if (PREDICT_FALSE(!s.ok())) {
        return exec_context_->Error(expr, s, ErrorCode::INVALID_ARGUMENTS);
      }
    } else {
      const Status s = PTExprToPB(expr, req->add_selected_exprs());
      if (PREDICT_FALSE(!s.ok())) {
        return exec_context_->Error(expr, s, ErrorCode::INVALID_ARGUMENTS);
      }

      // Add the expression metadata (rsrow descriptor).
      QLRSColDescPB *rscol_desc_pb = rsrow_desc_pb->add_rscol_descs();
      rscol_desc_pb->set_name(expr->QLName());
      expr->rscol_type_PB(rscol_desc_pb->mutable_ql_type());
    }
  }

  // Setup the column values that need to be read.
  Status s = ColumnRefsToPB(tnode, req->mutable_column_refs());
  if (PREDICT_FALSE(!s.ok())) {
    return exec_context_->Error(tnode, s, ErrorCode::INVALID_ARGUMENTS);
  }

  // Specify distinct columns or non.
  req->set_distinct(tnode->distinct());

  // Default row count limit is the page size.
  // We should return paging state when page size limit is hit.
  req->set_limit(params.page_size());
  req->set_return_paging_state(true);

  // Check if there is a limit and compute the new limit based on the number of returned rows.
  if (tnode->limit()) {
    int32_t limit;
    RETURN_NOT_OK(GetOffsetOrLimit(
        tnode,
        [](const PTSelectStmt* tnode) -> PTExpr::SharedPtr { return tnode->limit(); },
        "LIMIT", &limit));

    if (limit == 0 || params.total_num_rows_read() >= limit) {
      return Status::OK();
    }

    // If the LIMIT clause, subtracting the number of rows we have returned so far, is lower than
    // the page size limit set from above, set the lower limit and do not return paging state when
    // this limit is hit.
    limit -= params.total_num_rows_read();
    if (limit <= req->limit()) {
      req->set_limit(limit);
      req->set_return_paging_state(false);
    }
  }

  if (tnode->offset()) {
    int32_t offset;
    RETURN_NOT_OK(GetOffsetOrLimit(
        tnode,
        [](const PTSelectStmt *tnode) -> PTExpr::SharedPtr { return tnode->offset(); },
        "OFFSET", &offset));
    // Update the offset with values from previous pagination.
    offset = std::max(static_cast<int64_t>(0), offset - params.total_rows_skipped());
    req->set_offset(offset);
    // We need the paging state to know how many rows were skipped by the offset clause.
    req->set_return_paging_state(true);
  }

  // If this is a continuation of a prior read, set the next partition key, row key and total number
  // of rows read in the request's paging state.
  if (continue_select) {
    QLPagingStatePB *paging_state = req->mutable_paging_state();
    paging_state->set_next_partition_key(params.next_partition_key());
    paging_state->set_next_row_key(params.next_row_key());
    paging_state->set_total_num_rows_read(params.total_num_rows_read());
    paging_state->set_total_rows_skipped(params.total_rows_skipped());
  }

  // Set the consistency level for the operation. Always use strong consistency for system tables.
  select_op->set_yb_consistency_level(tnode->is_system() ? YBConsistencyLevel::STRONG
                                                         : params.yb_consistency_level());

  // If we have several hash partitions (i.e. IN condition on hash columns) we initialize the
  // start partition here, and then iteratively scan the rest in FetchMoreRows.
  // Otherwise, the request will already have the right hashed column values set.
  if (tnode_context->UnreadPartitionsRemaining() > 0) {
    tnode_context->InitializePartition(select_op->mutable_request(),
                                       continue_select ? params.next_partition_index() : 0);

    // We can optimize to run the ops in parallel (rather than serially) if:
    // - the estimated max number of rows is less than req limit (min of page size and CQL limit).
    // - there is no offset (which requires passing skipped rows from one request to the next).
    if (*max_rows_estimate <= req->limit() && !req->has_offset()) {
      RETURN_NOT_OK(AddOperation(select_op, tnode_context));
      while (tnode_context->UnreadPartitionsRemaining() > 1) {
        YBqlReadOpPtr op(table->NewQLSelect());
        op->mutable_request()->CopyFrom(select_op->request());
        op->set_yb_consistency_level(select_op->yb_consistency_level());
        tnode_context->AdvanceToNextPartition(op->mutable_request());
        RETURN_NOT_OK(AddOperation(op, tnode_context));
        select_op = op; // Use new op as base for the next one, if any.
      }
      return Status::OK();
    }
  }

  // Add the operation.
  return AddOperation(select_op, tnode_context);
}

Result<bool> Executor::FetchMoreRows(const PTSelectStmt* tnode,
                                     const YBqlReadOpPtr& op,
                                     TnodeContext* tnode_context,
                                     ExecContext* exec_context) {
  if (result_ == nullptr) {
    return STATUS(InternalError, "Missing result for SELECT operation");
  }

  // Rows read so far: in this fetch, previous fetches (for paging selects), and in total.
  RowsResult::SharedPtr current_result = std::static_pointer_cast<RowsResult>(result_);
  const size_t current_fetch_row_count =
      VERIFY_RESULT(QLRowBlock::GetRowCount(current_result->client(), current_result->rows_data()));
  const size_t previous_fetches_row_count = exec_context->params().total_num_rows_read();
  const size_t total_row_count = previous_fetches_row_count + current_fetch_row_count;

  // Statement (paging) parameters.
  StatementParameters current_params;
  RETURN_NOT_OK(current_params.set_paging_state(current_result->paging_state()));

  const size_t total_rows_skipped = exec_context->params().total_rows_skipped() +
                                    current_params.total_rows_skipped();

  // The limit for this select: min of page size and result limit (if set).
  uint64_t fetch_limit = exec_context->params().page_size(); // default;
  if (tnode->limit()) {
    QLExpressionPB limit_pb;
    RETURN_NOT_OK(PTExprToPB(tnode->limit(), &limit_pb));

    // If the LIMIT clause has been reached, we are done.
    if (total_row_count >= limit_pb.value().int32_value()) {
      current_result->clear_paging_state();
      return false;
    }

    const int64_t limit = limit_pb.value().int32_value() - previous_fetches_row_count;
    if (limit < fetch_limit) {
      fetch_limit = limit;
    }
  }

  //------------------------------------------------------------------------------------------------
  // Check if we should fetch more rows.

  // If there is no paging state the current scan has exhausted its results. The paging state
  // might be non-empty, but just contain num_rows_skipped, in this case the
  // 'next_partition_key' and 'next_row_key' would be empty indicating that we've finished
  // reading the current partition.
  const bool finished_current_read_partition = current_result->paging_state().empty() ||
                                               (current_params.next_partition_key().empty() &&
                                                current_params.next_row_key().empty());
  if (finished_current_read_partition) {

    // If there or no other partitions to query, we are done.
    if (tnode_context->UnreadPartitionsRemaining() <= 1) {
      // Clear the paging state, since we don't have any more data left in the table.
      current_result->clear_paging_state();
      return false;
    }

    // Sanity check that if we finished a partition the next partition/row key are empty.
    // Otherwise we could start scanning the next partition from the wrong place.
    DCHECK(current_params.next_partition_key().empty());
    DCHECK(current_params.next_row_key().empty());

    // Otherwise, we continue to the next partition.
    tnode_context->AdvanceToNextPartition(op->mutable_request());
  }

  // If we reached the fetch limit (min of paging state and limit clause) we are done.
  if (current_fetch_row_count >= fetch_limit) {

    // If we need to return a paging state to the user, we create it here so that we can resume from
    // the exact place where we left off: partition index and primary key within that partition.
    if (op->request().return_paging_state()) {
      QLPagingStatePB paging_state;
      paging_state.set_total_num_rows_read(total_row_count);
      paging_state.set_total_rows_skipped(total_rows_skipped);
      paging_state.set_table_id(tnode->table()->id());

      // Set the partition to resume from. Relevant for multi-partition selects, i.e. with IN
      // condition on the partition columns.
      paging_state.set_next_partition_index(tnode_context->current_partition_index());

      // Within a partition, set the exact primary key to resume from (if any).
      paging_state.set_next_partition_key(current_params.next_partition_key());
      paging_state.set_next_row_key(current_params.next_row_key());

      current_result->set_paging_state(paging_state);
    }

    return false;
  }

  //------------------------------------------------------------------------------------------------
  // Fetch more results.

  // Update limit, offset and paging_state information for next scan request.
  op->mutable_request()->set_limit(fetch_limit - current_fetch_row_count);
  if (tnode->offset()) {
    QLExpressionPB offset_pb;
    RETURN_NOT_OK(PTExprToPB(tnode->offset(), &offset_pb));
    // The paging state keeps a running count of the number of rows skipped so far.
    op->mutable_request()->set_offset(
        std::max(static_cast<int64_t>(0),
                 offset_pb.value().int32_value() - static_cast<int64_t>(total_rows_skipped)));
  }

  QLPagingStatePB *paging_state = op->mutable_request()->mutable_paging_state();
  paging_state->set_next_partition_key(current_params.next_partition_key());
  paging_state->set_next_row_key(current_params.next_row_key());
  paging_state->set_total_num_rows_read(total_row_count);
  paging_state->set_total_rows_skipped(total_rows_skipped);
  return true;
}

//--------------------------------------------------------------------------------------------------

Status Executor::ExecPTNode(const PTInsertStmt *tnode, TnodeContext* tnode_context) {
  // Create write request.
  const shared_ptr<client::YBTable>& table = tnode->table();
  YBqlWriteOpPtr insert_op(table->NewQLInsert());
  QLWriteRequestPB *req = insert_op->mutable_request();

  // Set the ttl.
  Status s = TtlToPB(tnode, req);
  if (PREDICT_FALSE(!s.ok())) {
    return exec_context_->Error(tnode, s, ErrorCode::INVALID_ARGUMENTS);
  }

  // Set the timestamp.
  s = TimestampToPB(tnode, req);
  if (PREDICT_FALSE(!s.ok())) {
    return exec_context_->Error(tnode, s, ErrorCode::INVALID_ARGUMENTS);
  }

  // Set the values for columns.
  s = ColumnArgsToPB(tnode, req);
  if (PREDICT_FALSE(!s.ok())) {
    return exec_context_->Error(tnode, s, ErrorCode::INVALID_ARGUMENTS);
  }

  // Setup the column values that need to be read.
  s = ColumnRefsToPB(tnode, req->mutable_column_refs());
  if (PREDICT_FALSE(!s.ok())) {
    return exec_context_->Error(tnode, s, ErrorCode::INVALID_ARGUMENTS);
  }

  // Set the IF clause.
  if (tnode->if_clause() != nullptr) {
    s = PTExprToPB(tnode->if_clause(), insert_op->mutable_request()->mutable_if_expr());
    if (PREDICT_FALSE(!s.ok())) {
      return exec_context_->Error(tnode->if_clause(), s, ErrorCode::INVALID_ARGUMENTS);
    }
    req->set_else_error(tnode->else_error());
  }

  // Set the RETURNS clause if set.
  if (tnode->returns_status()) {
    req->set_returns_status(true);
  }

  // Set whether write op writes to the static/primary row.
  insert_op->set_writes_static_row(tnode->ModifiesStaticRow());
  insert_op->set_writes_primary_row(tnode->ModifiesPrimaryRow());

  // Add the operation.
  return AddOperation(insert_op, tnode_context);
}

//--------------------------------------------------------------------------------------------------

Status Executor::ExecPTNode(const PTDeleteStmt *tnode, TnodeContext* tnode_context) {
  // Create write request.
  const shared_ptr<client::YBTable>& table = tnode->table();
  YBqlWriteOpPtr delete_op(table->NewQLDelete());
  QLWriteRequestPB *req = delete_op->mutable_request();

  // Set the timestamp.
  Status s = TimestampToPB(tnode, req);
  if (PREDICT_FALSE(!s.ok())) {
    return exec_context_->Error(tnode, s, ErrorCode::INVALID_ARGUMENTS);
  }

  // Where clause - Hash, range, and regular columns.
  // NOTE: Currently, where clause for write op doesn't allow regular columns.
  s = WhereClauseToPB(req, tnode->key_where_ops(), tnode->where_ops(),
                      tnode->subscripted_col_where_ops());
  if (PREDICT_FALSE(!s.ok())) {
    return exec_context_->Error(tnode, s, ErrorCode::INVALID_ARGUMENTS);
  }

  // Setup the column values that need to be read.
  s = ColumnRefsToPB(tnode, req->mutable_column_refs());
  if (PREDICT_FALSE(!s.ok())) {
    return exec_context_->Error(tnode, s, ErrorCode::INVALID_ARGUMENTS);
  }
  s = ColumnArgsToPB(tnode, req);
  if (PREDICT_FALSE(!s.ok())) {
    return exec_context_->Error(tnode, s, ErrorCode::INVALID_ARGUMENTS);
  }

  // Set the IF clause.
  if (tnode->if_clause() != nullptr) {
    s = PTExprToPB(tnode->if_clause(), delete_op->mutable_request()->mutable_if_expr());
    if (PREDICT_FALSE(!s.ok())) {
      return exec_context_->Error(tnode->if_clause(), s, ErrorCode::INVALID_ARGUMENTS);
    }
    req->set_else_error(tnode->else_error());
  }

  // Set the RETURNS clause if set.
  if (tnode->returns_status()) {
    req->set_returns_status(true);
  }

  // Set whether write op writes to the static/primary row.
  delete_op->set_writes_static_row(tnode->ModifiesStaticRow());
  delete_op->set_writes_primary_row(tnode->ModifiesPrimaryRow());

  // Add the operation.
  return AddOperation(delete_op, tnode_context);
}

//--------------------------------------------------------------------------------------------------

Status Executor::ExecPTNode(const PTUpdateStmt *tnode, TnodeContext* tnode_context) {
  // Create write request.
  const shared_ptr<client::YBTable>& table = tnode->table();
  YBqlWriteOpPtr update_op(table->NewQLUpdate());
  QLWriteRequestPB *req = update_op->mutable_request();

  // Where clause - Hash, range, and regular columns.
  // NOTE: Currently, where clause for write op doesn't allow regular columns.
  Status s = WhereClauseToPB(req, tnode->key_where_ops(), tnode->where_ops(),
                             tnode->subscripted_col_where_ops());
  if (PREDICT_FALSE(!s.ok())) {
    return exec_context_->Error(tnode, s, ErrorCode::INVALID_ARGUMENTS);
  }

  // Set the ttl.
  s = TtlToPB(tnode, req);
  if (PREDICT_FALSE(!s.ok())) {
    return exec_context_->Error(tnode, s, ErrorCode::INVALID_ARGUMENTS);
  }

  // Set the timestamp.
  s = TimestampToPB(tnode, req);
  if (PREDICT_FALSE(!s.ok())) {
    return exec_context_->Error(tnode, s, ErrorCode::INVALID_ARGUMENTS);
  }

  // Setup the columns' new values.
  s = ColumnArgsToPB(tnode, update_op->mutable_request());
  if (PREDICT_FALSE(!s.ok())) {
    return exec_context_->Error(tnode, s, ErrorCode::INVALID_ARGUMENTS);
  }

  // Setup the column values that need to be read.
  s = ColumnRefsToPB(tnode, req->mutable_column_refs());
  if (PREDICT_FALSE(!s.ok())) {
    return exec_context_->Error(tnode, s, ErrorCode::INVALID_ARGUMENTS);
  }

  // Set the IF clause.
  if (tnode->if_clause() != nullptr) {
    s = PTExprToPB(tnode->if_clause(), update_op->mutable_request()->mutable_if_expr());
    if (PREDICT_FALSE(!s.ok())) {
      return exec_context_->Error(tnode->if_clause(), s, ErrorCode::INVALID_ARGUMENTS);
    }
    req->set_else_error(tnode->else_error());
  }

  // Set the RETURNS clause if set.
  if (tnode->returns_status()) {
    req->set_returns_status(true);
  }

  // Set whether write op writes to the static/primary row.
  update_op->set_writes_static_row(tnode->ModifiesStaticRow());
  update_op->set_writes_primary_row(tnode->ModifiesPrimaryRow());

  // Add the operation.
  return AddOperation(update_op, tnode_context);
}

//--------------------------------------------------------------------------------------------------

Status Executor::ExecPTNode(const PTStartTransaction *tnode) {
  exec_context_->StartTransaction(tnode->isolation_level(), ql_env_);
  return Status::OK();
}

//--------------------------------------------------------------------------------------------------

Status Executor::ExecPTNode(const PTCommit *tnode) {
  // Commit happens after the write operations have been flushed and responded.
  return Status::OK();
}

//--------------------------------------------------------------------------------------------------

Status Executor::ExecPTNode(const PTTruncateStmt *tnode) {
  return ql_env_->TruncateTable(tnode->table_id());
}

//--------------------------------------------------------------------------------------------------

Status Executor::ExecPTNode(const PTCreateKeyspace *tnode) {
  Status s = ql_env_->CreateKeyspace(tnode->name());

  if (PREDICT_FALSE(!s.ok())) {
    ErrorCode error_code = ErrorCode::SERVER_ERROR;

    if (s.IsAlreadyPresent()) {
      if (tnode->create_if_not_exists()) {
        // Case: CREATE KEYSPACE IF NOT EXISTS name;
        return Status::OK();
      }

      error_code = ErrorCode::KEYSPACE_ALREADY_EXISTS;
    }

    return exec_context_->Error(tnode, s, error_code);
  }

  result_ = std::make_shared<SchemaChangeResult>("CREATED", "KEYSPACE", tnode->name());
  return Status::OK();
}

//--------------------------------------------------------------------------------------------------

Status Executor::ExecPTNode(const PTUseKeyspace *tnode) {
  const Status s = ql_env_->UseKeyspace(tnode->name());
  if (PREDICT_FALSE(!s.ok())) {
    ErrorCode error_code = s.IsNotFound() ? ErrorCode::KEYSPACE_NOT_FOUND : ErrorCode::SERVER_ERROR;
    return exec_context_->Error(tnode, s, error_code);
  }

  result_ = std::make_shared<SetKeyspaceResult>(tnode->name());
  return Status::OK();
}

//--------------------------------------------------------------------------------------------------

namespace {

// When executing a DML in a transaction or a SELECT statement on a transaction-enabled table, the
// following transient errors may happen for which the YCQL service will restart the transaction.
// - TryAgain: when the transaction has a write conflict with another transaction or read-
//             restart is required.
// - Expired:  when the transaction expires due to missed heartbeat
// - TimedOut: when a tablet times out while contacting the transaction status tablet
bool NeedsRestart(const Status& s) {
  return s.IsTryAgain() || s.IsExpired() || s.IsTimedOut();
}

} // namespace

void Executor::FlushAsync() {
  // Buffered read/write operations are flushed in rounds. In each round, FlushAsync() is called to
  // flush buffered operations in the non-transactional session in the Executor or the transactional
  // session in each ExecContext if any. Also, transactions in any ExecContext ready to commit with
  // no more pending operation are also be committed. If there is no session to flush nor
  // transaction to commit, the statement is executed.
  //
  // As flushes and commits happen, multiple FlushAsyncDone() and CommitDone() callbacks can be
  // invoked concurrently. To avoid race condition among them, the async-call count before calling
  // FlushAsync() and CommitTransaction(). This is necessary so that only the last callback will
  // correctly detect that all async calls are done invoked before processing the async results
  // exclusively.
  write_batch_.Clear();
  std::vector<std::pair<YBSessionPtr, ExecContext*>> flush_sessions;
  std::vector<ExecContext*> commit_contexts;
  if (session_->CountBufferedOperations() > 0) {
    flush_sessions.push_back({session_, nullptr});
  }
  for (ExecContext& exec_context : exec_contexts_) {
    if (exec_context.HasTransaction()) {
      if (exec_context.transactional_session()->CountBufferedOperations() > 0) {
        flush_sessions.push_back({exec_context.transactional_session(), &exec_context});
      } else if (!exec_context.HasPendingOperations()) {
        commit_contexts.push_back(&exec_context);
      }
    }
  }

  // Commit transactions first before flushing operations in case some operations are blocked by
  // prior operations in the uncommitted transactions. num_flushes_ is updated before FlushAsync()
  // and CommitTransaction() are called to avoid race condition of recursive FlushAsync() called
  // from FlushAsyncDone() and CommitDone().
  DCHECK_EQ(num_async_calls_, 0);
  num_async_calls_ = flush_sessions.size() + commit_contexts.size();
  num_flushes_ += flush_sessions.size();
  async_status_ = Status::OK();
  for (auto* exec_context : commit_contexts) {
    exec_context->CommitTransaction([this, exec_context](const Status& s) {
        CommitDone(s, exec_context);
      });
  }
  for (const auto& pair : flush_sessions) {
    auto session = pair.first;
    auto exec_context = pair.second;
    TRACE("Flush Async");
    session->FlushAsync([this, exec_context](const Status& s) {
        FlushAsyncDone(s, exec_context);
      });
  }

  if (flush_sessions.empty() && commit_contexts.empty()) {
    // If this is a batch returning status, append the rows in the user-given order before
    // returning result.
    if (IsReturnsStatusBatch()) {
      for (ExecContext& exec_context : exec_contexts_) {
        int64_t row_count = 0;
        for (TnodeContext& tnode_context : exec_context.tnode_contexts()) {
          for (client::YBqlOpPtr& op : tnode_context.ops()) {
            if (!op->rows_data().empty()) {
              DCHECK_EQ(++row_count, 1) << exec_context.stmt() << " returns multiple status row";
              RETURN_STMT_NOT_OK(AppendResult(std::make_shared<RowsResult>(op.get())));
            }
          }
        }
      }
    }
    return StatementExecuted(Status::OK());
  }
}

// As multiple FlushAsyncDone() and CommitDone() can be invoked concurrently for different
// ExecContexts, care must be taken so that the callbacks only update the individual ExecContexts.
// Any update on data structures shared in Executor should either be protected by a mutex or
// deferred to ProcessAsyncResults() that will be invoked exclusively.
void Executor::FlushAsyncDone(Status s, ExecContext* exec_context) {
  TRACE("Flush Async Done");
  // Process FlushAsync status for either transactional session in an ExecContext, or the
  // non-transactional session in the Executor for other ExecContexts with no transactional session.
  const int64_t num_async_calls = --num_async_calls_;
  const YBSessionPtr& session = exec_context != nullptr ? GetSession(exec_context) : session_;

  // When any error occurs during the dispatching of YBOperation, YBSession saves the error and
  // returns IOError. When it happens, retrieves the errors and discard the IOError.
  OpErrors op_errors;
  if (s.IsIOError()) {
    for (const auto& error : session->GetPendingErrors()) {
      op_errors[static_cast<const client::YBqlOp*>(&error->failed_op())] = error->status();
    }
    s = Status::OK();
  }

  if (s.ok()) {
    if (exec_context != nullptr) {
      s = ProcessAsyncStatus(op_errors, exec_context);
      if (!s.ok()) {
        std::lock_guard<std::mutex> lock(status_mutex_);
        async_status_ = s;
      }
    } else {
      for (auto& exec_context : exec_contexts_) {
        if (!exec_context.HasTransaction()) {
          s = ProcessAsyncStatus(op_errors, &exec_context);
          if (!s.ok()) {
            std::lock_guard<std::mutex> lock(status_mutex_);
            async_status_ = s;
          }
        }
      }
    }
  } else {
    std::lock_guard<std::mutex> lock(status_mutex_);
    async_status_ = s;
  }

  // Process async results exclusively if this is the last callback of the last FlushAsync() and
  // there is no more outstanding async call.
  if (num_async_calls == 0) {
    ProcessAsyncResults();
  }
}

void Executor::CommitDone(Status s, ExecContext* exec_context) {
  TRACE("Commit Transaction Done");
  const int64_t num_async_calls = --num_async_calls_;
  if (s.ok()) {
    if (ql_metrics_ != nullptr) {
      const MonoTime now = MonoTime::Now();
      const auto delta_usec = (now - exec_context->transaction_start_time()).ToMicroseconds();
      ql_metrics_->ql_transaction_->Increment(delta_usec);
    }
  } else {
    if (NeedsRestart(s)) {
      exec_context->Reset(client::Restart::kTrue);
    } else {
      std::lock_guard<std::mutex> lock(status_mutex_);
      async_status_ = s;
    }
  }

  // Process async results exclusively if this is the last callback of the last FlushAsync() and
  // there is no more outstanding async call.
  if (num_async_calls == 0) {
    ProcessAsyncResults();
  }
}

void Executor::ProcessAsyncResults(const bool rescheduled) {
  // If the current thread is not the RPC worker thread, call the callback directly. Otherwise,
  // reschedule the call to resume in the RPC worker thread.
  if (!rescheduled && !rpc::ThreadPool::IsCurrentThreadRpcWorker()) {
    return rescheduler_([this]() { ProcessAsyncResults(true /* rescheduled */); });
  }

  // Return error immediately when async call failed.
  RETURN_STMT_NOT_OK(async_status_);

  // Go through each ExecContext and process async results.
  bool has_buffered_ops = false;
  const MonoTime now = (ql_metrics_ != nullptr) ? MonoTime::Now() : MonoTime();
  for (auto exec_itr = exec_contexts_.begin(); exec_itr != exec_contexts_.end(); ) {

    // Set current ExecContext.
    exec_context_ = &*exec_itr;

    // Restart a statement if necessary
    if (exec_context_->restart()) {
      const TreeNode *root = exec_context_->parse_tree().root().get();
      // Clear partial rows accumulated from the SELECT statement.
      if (root->opcode() == TreeNodeOpcode::kPTSelectStmt) {
        result_ = nullptr;
      }
      YBSessionPtr session = GetSession(exec_context_);
      session->SetReadPoint(client::Restart::kTrue);
      RETURN_STMT_NOT_OK(ExecTreeNode(root));
      if (session->CountBufferedOperations() > 0) {
        has_buffered_ops = true;
      }
      exec_itr++;
      continue;
    }

    // Go through each TnodeContext in an ExecContext and process async results.
    auto& tnode_contexts = exec_context_->tnode_contexts();
    for (auto tnode_itr = tnode_contexts.begin(); tnode_itr != tnode_contexts.end(); ) {
      TnodeContext& tnode_context = *tnode_itr;

      const Result<bool> result = ProcessTnodeResults(&tnode_context);
      RETURN_STMT_NOT_OK(result);
      if (*result) {
        has_buffered_ops = true;
      }

      // If this statement is restarted, stop traversing the rest of the statement tnodes.
      if (exec_context_->restart()) {
        break;
      }

      // If there are pending ops, we are not done with this statement tnode yet.
      if (tnode_context.HasPendingOperations()) {
        tnode_itr++;
        continue;
      }

      // For SELECT statement, aggregate result sets if needed.
      const TreeNode *tnode = tnode_context.tnode();
      if (tnode->opcode() == TreeNodeOpcode::kPTSelectStmt) {
        RETURN_STMT_NOT_OK(AggregateResultSets(static_cast<const PTSelectStmt *>(tnode)));
      }

      // Update the metrics for SELECT/INSERT/UPDATE/DELETE here after the ops have been completed
      // but exclude the time to commit the transaction if any. Report the metric only once.
      if (ql_metrics_ != nullptr && !tnode_context.end_time().Initialized()) {
        tnode_context.set_end_time(now);
        const auto delta_usec = tnode_context.execution_time().ToMicroseconds();
        switch (tnode->opcode()) {
          case TreeNodeOpcode::kPTSelectStmt:
            ql_metrics_->ql_select_->Increment(delta_usec);
            break;
          case TreeNodeOpcode::kPTInsertStmt:
            ql_metrics_->ql_insert_->Increment(delta_usec);
            break;
          case TreeNodeOpcode::kPTUpdateStmt:
            ql_metrics_->ql_update_->Increment(delta_usec);
            break;
          case TreeNodeOpcode::kPTDeleteStmt:
            ql_metrics_->ql_delete_->Increment(delta_usec);
            break;
          case TreeNodeOpcode::kPTStartTransaction:
          case TreeNodeOpcode::kPTCommit:
            break;
          default:
            LOG(FATAL) << "unexpected operation " << tnode->opcode();
        }
        if (tnode->IsDml()) {
          ql_metrics_->time_to_execute_ql_query_->Increment(delta_usec);
        }
      }

      // If this is a batch returning status, keep the statement tnode with its ops so that we can
      // return the row status when all statements in the batch finish.
      if (IsReturnsStatusBatch()) {
        tnode_itr++;
        continue;
      }

      // Remove the statement tnode that has completed.
      tnode_itr = tnode_contexts.erase(tnode_itr);
    }

    // If the current ExecContext is restarted, process it again. Otherwise, move to the next one.
    if (!exec_context_->restart()) {
      exec_itr++;
    }
  }

  // If there are buffered ops that need flushes, the flushes need to be rescheduled if this call
  // hasn't been rescheduled. This is necessary because in an RF1 setup, the flush is a direct
  // local call and the recursive FlushAsync and FlushAsyncDone calls for a full table scan can
  // recurse too deeply hitting the stack limitiation. Rescheduling can also avoid occupying the
  // RPC worker thread for too long starving other CQL calls waiting in the queue. If there is no
  // buffered ops to flush, just call FlushAsync() to commit the transactions if any.
  if (has_buffered_ops && !rescheduled) {
    rescheduler_([this]() { FlushAsync(); });
  } else {
    FlushAsync();
  }
}

Result<bool> Executor::ProcessTnodeResults(TnodeContext* tnode_context) {
  bool has_buffered_ops = false;

  // Go through each op in a TnodeContext and process async results.
  const TreeNode *tnode = tnode_context->tnode();
  auto& ops = tnode_context->ops();
  for (auto op_itr = ops.begin(); op_itr != ops.end(); ) {
    YBqlOpPtr& op = *op_itr;

    // Apply any op that has not been applied and executed.
    if (!op->response().has_status()) {
      DCHECK_EQ(op->type(), YBOperation::Type::QL_WRITE);
      if (write_batch_.Add(std::static_pointer_cast<YBqlWriteOp>(op))) {
        YBSessionPtr session = GetSession(exec_context_);
        TRACE("Apply");
        RETURN_NOT_OK(session->Apply(op));
        has_buffered_ops = true;
      }
      op_itr++;
      continue;
    }

    // If the statement is in a transaction, check the status of the current operation. If it
    // failed to apply (either because of an execution error or unsatisfied IF condition), quit the
    // execution and abort the transaction. Also, if this is a batch returning status, mark all
    // other ops in this statement as done and clear the rows data to make sure only one status row
    // is returned from this statement.
    //
    // Note: For an error response, we only get to this point if using 'RETURNS STATUS AS ROW'.
    // Otherwise, ProcessAsyncResults() should have failed so we would have returned above already.
    if (exec_context_->HasTransaction() &&
        (op->response().status() != QLResponsePB_QLStatus_YQL_STATUS_OK ||
         (op->response().has_applied() && !op->response().applied()))) {
      exec_context_->AbortTransaction();
      if (IsReturnsStatusBatch()) {
        for (auto& tnode_context2 : exec_context_->tnode_contexts()) {
          for (auto& op2 : tnode_context2.ops()) {
            if (op2 != op) {
              op2->mutable_response()->set_status(QLResponsePB::YQL_STATUS_OK);
              op2->rows_data().clear();
            }
          }
        }
      }
    }

    // If the transaction is ready to commit, apply child transaction results if any.
    if (exec_context_->HasTransaction() && !exec_context_->HasPendingOperations()) {
      const QLResponsePB& response = op->response();
      if (response.has_child_transaction_result()) {
        const auto& result = response.child_transaction_result();
        const Status s = exec_context_->ApplyChildTransactionResult(result);
        if (NeedsRestart(s)) {
          exec_context_->Reset(client::Restart::kTrue);
          break;
        }
        RETURN_NOT_OK(s);
      }
    }

    // If this is a batch returning status, defer appending the row because we need to return the
    // results in the user-given order when all statements in the batch finish.
    if (IsReturnsStatusBatch()) {
      op_itr++;
      continue;
    }

    // Append the row if present.
    if (!op->rows_data().empty()) {
      RETURN_NOT_OK(AppendResult(std::make_shared<RowsResult>(op.get())));
    }

    // For SELECT statement, check if there are more rows to fetch and apply the op as needed.
    if (tnode->opcode() == TreeNodeOpcode::kPTSelectStmt) {
      const auto* select_stmt = static_cast<const PTSelectStmt *>(tnode);
      DCHECK_EQ(op->type(), YBOperation::Type::QL_READ);
      const auto& read_op = std::static_pointer_cast<YBqlReadOp>(op);
      if (VERIFY_RESULT(FetchMoreRows(select_stmt, read_op, tnode_context, exec_context_))) {
        op->mutable_response()->Clear();
        TRACE("Apply");
        RETURN_NOT_OK(session_->Apply(op));
        has_buffered_ops = true;
        op_itr++;
        continue;
      }
    }

    // Remove the op that has completed.
    op_itr = ops.erase(op_itr);
  }

  return has_buffered_ops;
}

//--------------------------------------------------------------------------------------------------

namespace {

// Check if index updates can be issued from CQL proxy directly when executing a DML. Only indexes
// that index primary key columns only may be updated from CQL proxy.
bool UpdateIndexesLocally(const PTDmlStmt *tnode, const QLWriteRequestPB& req) {
  if (req.has_if_expr() || req.returns_status()) {
    return false;
  }

  switch (req.type()) {
    // For insert, the pk-only indexes can be updated from CQL proxy directly.
    case QLWriteRequestPB::QL_STMT_INSERT:
      return true;

    // For update, the pk-only indexes can be updated from CQL proxy directly only when not all
    // columns are set to null. Otherwise, the row may be removed by the DML if it was created via
    // update (i.e. no liveness column) and the remaining columns are already null.
    case QLWriteRequestPB::QL_STMT_UPDATE: {
      for (const auto& column_value : req.column_values()) {
        switch (column_value.expr().expr_case()) {
          case QLExpressionPB::ExprCase::kValue:
            if (!IsNull(column_value.expr().value())) {
              return true;
            }
            break;
          case QLExpressionPB::ExprCase::kColumnId: FALLTHROUGH_INTENDED;
          case QLExpressionPB::ExprCase::kSubscriptedCol: FALLTHROUGH_INTENDED;
          case QLExpressionPB::ExprCase::kJsonColumn: FALLTHROUGH_INTENDED;
          case QLExpressionPB::ExprCase::kBfcall: FALLTHROUGH_INTENDED;
          case QLExpressionPB::ExprCase::kTscall: FALLTHROUGH_INTENDED;
          case QLExpressionPB::ExprCase::kCondition: FALLTHROUGH_INTENDED;
          case QLExpressionPB::ExprCase::kBocall: FALLTHROUGH_INTENDED;
          case QLExpressionPB::ExprCase::kBindId: FALLTHROUGH_INTENDED;
          case QLExpressionPB::ExprCase::EXPR_NOT_SET:
            return false;
        }
      }
      return false;
    }
    // For delete, the pk-only indexes can be updated from CQL proxy directly only if the whole
    // is deleted and it is not a range delete.
    case QLWriteRequestPB::QL_STMT_DELETE: {
      const Schema& schema = tnode->table()->InternalSchema();
      return (req.column_values().empty() &&
              req.range_column_values_size() == schema.num_range_key_columns());
    }
  }
  return false; // Not feasible
}

} // namespace

Status Executor::UpdateIndexes(const PTDmlStmt *tnode,
                               QLWriteRequestPB *req,
                               TnodeContext* tnode_context) {
  // DML with TTL is not allowed if indexes are present.
  if (req->has_ttl()) {
    return exec_context_->Error(tnode, ErrorCode::FEATURE_NOT_SUPPORTED);
  }

  // If updates of pk-only indexes can be issued from CQL proxy directly, do it. Otherwise, add
  // them to the list of indexes to be updated from tserver.
  if (!tnode->pk_only_indexes().empty()) {
    if (UpdateIndexesLocally(tnode, *req)) {
      RETURN_NOT_OK(AddIndexWriteOps(tnode, *req, tnode_context));
    } else {
      for (const auto& index : tnode->pk_only_indexes()) {
        req->add_update_index_ids(index->id());
      }
    }
  }

  // Add non-pk-only indexes to the list of indexes to be updated from tserver also.
  for (const auto& index_id : tnode->non_pk_only_indexes()) {
    req->add_update_index_ids(index_id);
  }

  // For update/delete, check if it just deletes some columns. If so, add the rest columns to be
  // read so that tserver can check if they are all null also, in which case the row will be
  // removed after the DML.
  if ((req->type() == QLWriteRequestPB::QL_STMT_UPDATE ||
       req->type() == QLWriteRequestPB::QL_STMT_DELETE) &&
      !req->column_values().empty()) {
    bool all_null = true;
    std::set<int32> column_dels;
    for (const QLColumnValuePB& column_value : req->column_values()) {
      if (column_value.has_expr() &&
          column_value.expr().has_value() &&
          !IsNull(column_value.expr().value())) {
        all_null = false;
        break;
      }
      column_dels.insert(column_value.column_id());
    }
    if (all_null) {
      const Schema& schema = tnode->table()->InternalSchema();
      const MCSet<int32>& column_refs = tnode->column_refs();
      for (size_t idx = schema.num_key_columns(); idx < schema.num_columns(); idx++) {
        const int32 column_id = schema.column_id(idx);
        if (!schema.column(idx).is_static() &&
            column_refs.count(column_id) != 0 &&
            column_dels.count(column_id) != 0) {
          req->mutable_column_refs()->add_ids(column_id);
        }
      }
    }
  }

  if (!req->update_index_ids().empty() && tnode->RequiresTransaction()) {
    RETURN_NOT_OK(exec_context_->PrepareChildTransaction(req->mutable_child_transaction_data()));
  }
  return Status::OK();
}

// Add the write operations to update the pk-only indexes.
Status Executor::AddIndexWriteOps(const PTDmlStmt *tnode,
                                  const QLWriteRequestPB& req,
                                  TnodeContext* tnode_context) {
  const Schema& schema = tnode->table()->InternalSchema();
  const bool is_upsert = (req.type() == QLWriteRequestPB::QL_STMT_INSERT ||
                          req.type() == QLWriteRequestPB::QL_STMT_UPDATE);
  // Populate a column-id to value map.
  std::unordered_map<ColumnId, const QLExpressionPB&> values;
  for (size_t i = 0; i < schema.num_hash_key_columns(); i++) {
    values.emplace(schema.column_id(i), req.hashed_column_values(i));
  }
  for (size_t i = 0; i < schema.num_range_key_columns(); i++) {
    values.emplace(schema.column_id(schema.num_hash_key_columns() + i), req.range_column_values(i));
  }
  if (is_upsert) {
    for (const auto& column_value : req.column_values()) {
      values.emplace(ColumnId(column_value.column_id()), column_value.expr());
    }
  }

  // Create the write operation for each index and populate it using the original operation.
  for (const auto& index_table : tnode->pk_only_indexes()) {
    const IndexInfo* index =
        VERIFY_RESULT(tnode->table()->index_map().FindIndex(index_table->id()));
    YBqlWriteOpPtr index_op(is_upsert ? index_table->NewQLInsert() : index_table->NewQLDelete());
    index_op->set_writes_primary_row(true);
    QLWriteRequestPB *index_req = index_op->mutable_request();
    index_req->set_request_id(req.request_id());
    index_req->set_query_id(req.query_id());
    for (size_t i = 0; i < index->columns().size(); i++) {
      const ColumnId indexed_column_id = index->column(i).indexed_column_id;
      if (i < index->hash_column_count()) {
        *index_req->add_hashed_column_values() = values.at(indexed_column_id);
      } else if (i < index->key_column_count()) {
        *index_req->add_range_column_values() = values.at(indexed_column_id);
      } else if (is_upsert) {
        const auto itr = values.find(indexed_column_id);
        if (itr != values.end()) {
          QLColumnValuePB* column_value = index_req->add_column_values();
          column_value->set_column_id(index->column(i).column_id);
          *column_value->mutable_expr() = itr->second;
        }
      }
    }
    RETURN_NOT_OK(AddOperation(index_op, tnode_context));
  }

  return Status::OK();
}

//--------------------------------------------------------------------------------------------------

bool Executor::WriteBatch::Add(const YBqlWriteOpPtr& op) {
  // Checks if the write operation reads the primary/static row and if another operation that writes
  // the primary/static row by the same primary/hash key already exists.
  if ((op->ReadsPrimaryRow() && ops_by_primary_key_.count(op) > 0) ||
      (op->ReadsStaticRow() && ops_by_hash_key_.count(op) > 0)) {
    return false;
  }

  if (op->WritesPrimaryRow()) { ops_by_primary_key_.insert(op); }
  if (op->WritesStaticRow()) { ops_by_hash_key_.insert(op); }
  return true;
}

void Executor::WriteBatch::Clear() {
  ops_by_primary_key_.clear();
  ops_by_hash_key_.clear();
}

bool Executor::WriteBatch::Empty() const {
  return ops_by_primary_key_.empty() &&  ops_by_hash_key_.empty();
}

//--------------------------------------------------------------------------------------------------

Status Executor::AddOperation(const YBqlReadOpPtr& op, TnodeContext *tnode_context) {
  DCHECK(write_batch_.Empty()) << "Concurrent read and write operations not supported yet";
  tnode_context->AddOperation(op);

  TRACE("Apply");
  return session_->Apply(op);
}

Status Executor::AddOperation(const YBqlWriteOpPtr& op, TnodeContext *tnode_context) {
  tnode_context->AddOperation(op);

  // Check for inter-dependency in the current write batch before applying the write operation.
  // Apply it in the transactional session in exec_context for the current statement if there is
  // one. Otherwise, apply to the non-transactional session in the executor.
  if (write_batch_.Add(op)) {
    YBSessionPtr session = GetSession(exec_context_);
    TRACE("Apply");
    RETURN_NOT_OK(session->Apply(op));
  }

  // Also update secondary indexes if needed.
  if (op->table()->index_map().empty()) {
    return Status::OK();
  }
  const auto* dml_stmt = static_cast<const PTDmlStmt*>(tnode_context->tnode());
  return UpdateIndexes(dml_stmt, op->mutable_request(), tnode_context);
}

//--------------------------------------------------------------------------------------------------

Status Executor::ProcessStatementStatus(const ParseTree& parse_tree, const Status& s) {
  if (PREDICT_FALSE(!s.ok() && s.IsQLError() && !parse_tree.reparsed())) {
    // If execution fails because the statement was analyzed with stale metadata cache, the
    // statement needs to be reparsed and re-analyzed. Symptoms of stale metadata are as listed
    // below. Expand the list in future as new cases arise.
    // - TABLET_NOT_FOUND when the tserver fails to execute the YBQLOp because the tablet is not
    //   found (ENG-945).
    // - WRONG_METADATA_VERSION when the schema version the tablet holds is different from the one
    //   used by the semantic analyzer.
    // - INVALID_TABLE_DEFINITION when a referenced user-defined type is not found.
    // - INVALID_ARGUMENTS when the column datatype is inconsistent with the supplied value in an
    //   INSERT or UPDATE statement.
    const ErrorCode errcode = GetErrorCode(s);
    if (errcode == ErrorCode::TABLET_NOT_FOUND         ||
        errcode == ErrorCode::WRONG_METADATA_VERSION   ||
        errcode == ErrorCode::INVALID_TABLE_DEFINITION ||
        errcode == ErrorCode::INVALID_ARGUMENTS        ||
        errcode == ErrorCode::TABLE_NOT_FOUND          ||
        errcode == ErrorCode::TYPE_NOT_FOUND) {
      parse_tree.ClearAnalyzedTableCache(ql_env_);
      parse_tree.ClearAnalyzedUDTypeCache(ql_env_);
      parse_tree.set_stale();
      return ErrorStatus(ErrorCode::STALE_METADATA);
    }
  }
  return s;
}

Status Executor::ProcessOpStatus(const PTDmlStmt* stmt,
                                 const YBqlOpPtr& op,
                                 ExecContext* exec_context) {
  const QLResponsePB &resp = op->response();
  // Returns if this op was deferred and has not been completed, or it has been completed okay.
  if (!resp.has_status() || resp.status() == QLResponsePB::YQL_STATUS_OK) {
    return Status::OK();
  }

  if (resp.status() == QLResponsePB::YQL_STATUS_RESTART_REQUIRED_ERROR) {
    return STATUS(TryAgain, resp.error_message());
  }

  // If we got an error we need to manually produce a result in the op.
  if (stmt->IsWriteOp() && stmt->returns_status()) {
    std::vector<ColumnSchema> columns;
    const auto& schema = stmt->table()->schema();
    columns.reserve(stmt->table()->schema().num_columns() + 2);
    columns.emplace_back("[applied]", DataType::BOOL);
    columns.emplace_back("[message]", DataType::STRING);
    columns.insert(columns.end(), schema.columns().begin(), schema.columns().end());
    auto* column_schemas = op->mutable_response()->mutable_column_schemas();
    column_schemas->Clear();
    for (const auto& column : columns) {
      ColumnSchemaToPB(column, column_schemas->Add());
    }

    QLRowBlock result_row_block(Schema(columns, 0));
    QLRow& row = result_row_block.Extend();
    row.mutable_column(0)->set_bool_value(false);
    row.mutable_column(1)->set_string_value(resp.error_message());
    // Leave the rest of the columns null in this case.

    faststring row_data;
    result_row_block.Serialize(YQL_CLIENT_CQL, &row_data);
    *op->mutable_rows_data() = row_data.ToString();
    return Status::OK();
  }

  const ErrorCode errcode = QLStatusToErrorCode(resp.status());
  return exec_context->Error(stmt, resp.error_message().c_str(), errcode);
}

Status Executor::ProcessAsyncStatus(const OpErrors& op_errors, ExecContext* exec_context) {
  for (auto& tnode_context : exec_context->tnode_contexts()) {
    const TreeNode* tnode = tnode_context.tnode();
    for (auto& op : tnode_context.ops()) {
      Status s;
      const auto itr = op_errors.find(op.get());
      if (itr != op_errors.end()) {
        s = itr->second;
      }
      if (PREDICT_FALSE(!s.ok() && !NeedsRestart(s))) {
        // YBOperation returns not-found error when the tablet is not found.
        const auto errcode = s.IsNotFound() ? ErrorCode::TABLET_NOT_FOUND : ErrorCode::EXEC_ERROR;
        s = exec_context->Error(tnode, s, errcode);
      }
      if (s.ok()) {
        DCHECK(tnode->IsDml()) << "Only DML should issue a read/write operation";
        s = ProcessOpStatus(static_cast<const PTDmlStmt *>(tnode), op, exec_context);
      }
      if (NeedsRestart(s)) {
        exec_context->Reset(client::Restart::kTrue);
        return Status::OK();
      }
      RETURN_NOT_OK(ProcessStatementStatus(exec_context->parse_tree(), s));
    }
  }
  return Status::OK();
}

Status Executor::AppendResult(const RowsResult::SharedPtr& result) {
  CHECK(result) << "No result to append";
  if (result_ == nullptr) {
    result_ = result;
    return Status::OK();
  }
  CHECK(result_->type() == ExecutedResult::Type::ROWS);
  return std::static_pointer_cast<RowsResult>(result_)->Append(*result);
}

void Executor::StatementExecuted(const Status& s) {
  // Update metrics for all statements executed.
  if (s.ok() && ql_metrics_ != nullptr) {
    for (auto& exec_context : exec_contexts_) {
      for (auto& tnode_context : exec_context.tnode_contexts()) {
        const TreeNode* tnode = tnode_context.tnode();
        if (tnode != nullptr) {
          switch (tnode->opcode()) {
            case TreeNodeOpcode::kPTSelectStmt: FALLTHROUGH_INTENDED;
            case TreeNodeOpcode::kPTInsertStmt: FALLTHROUGH_INTENDED;
            case TreeNodeOpcode::kPTUpdateStmt: FALLTHROUGH_INTENDED;
            case TreeNodeOpcode::kPTDeleteStmt: FALLTHROUGH_INTENDED;
            case TreeNodeOpcode::kPTListNode:   FALLTHROUGH_INTENDED;
            case TreeNodeOpcode::kPTStartTransaction: FALLTHROUGH_INTENDED;
            case TreeNodeOpcode::kPTCommit:
              // The metrics for SELECT/INSERT/UPDATE/DELETE have been updated when the ops have
              // been completed in FlushAsyncDone(). Exclude PTListNode also as we are interested
              // in the metrics of its constituent DMLs only. Transaction metrics have been
              // updated in CommitDone().
              break;
            default: {
              const MonoTime now = MonoTime::Now();
              const auto delta_usec = (now - tnode_context.start_time()).ToMicroseconds();
              ql_metrics_->ql_others_->Increment(delta_usec);
              ql_metrics_->time_to_execute_ql_query_->Increment(delta_usec);
              break;
            }
          }
        }
      }
      ql_metrics_->num_retries_to_execute_ql_->Increment(exec_context.num_retries());
    }
    ql_metrics_->num_flushes_to_execute_ql_->Increment(num_flushes_);
  }

  // Clean up and invoke statement-executed callback.
  ExecutedResult::SharedPtr result = s.ok() ? std::move(result_) : nullptr;
  StatementExecutedCallback cb = std::move(cb_);
  Reset();
  cb.Run(s, result);
}

void Executor::Reset() {
  exec_context_ = nullptr;
  exec_contexts_.clear();
  write_batch_.Clear();
  session_->Abort();
  num_flushes_ = 0;
  result_ = nullptr;
  cb_.Reset();
  returns_status_batch_opt_ = boost::none;
}

}  // namespace ql
}  // namespace yb
