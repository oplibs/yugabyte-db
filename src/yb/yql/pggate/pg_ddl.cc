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

#include "yb/yql/pggate/pg_ddl.h"

#include "yb/client/table_alterer.h"
#include "yb/client/table_creator.h"
#include "yb/client/namespace_alterer.h"
#include "yb/client/yb_op.h"

#include "yb/common/common.pb.h"
#include "yb/common/common_flags.h"
#include "yb/common/entity_ids.h"
#include "yb/common/pg_system_attr.h"
#include "yb/docdb/doc_key.h"
#include "yb/docdb/primitive_value.h"
#include "yb/util/flag_tags.h"

#include "yb/yql/pggate/pg_client.h"

DEFINE_test_flag(int32, user_ddl_operation_timeout_sec, 0,
                 "Adjusts the timeout for a DDL operation from the YBClient default, if non-zero.");

namespace yb {
namespace pggate {

using std::make_shared;
using std::shared_ptr;
using std::string;
using namespace std::literals;  // NOLINT

using client::YBClient;
using client::YBSession;
using client::YBMetaDataCache;

// TODO(neil) This should be derived from a GFLAGS.
static MonoDelta kSessionTimeout = 60s;

namespace {

CoarseTimePoint DdlDeadline() {
  auto timeout = MonoDelta::FromSeconds(FLAGS_TEST_user_ddl_operation_timeout_sec);
  if (timeout == MonoDelta::kZero) {
    // TODO(PG_CLIENT)
    timeout = 120s;
  }
  return CoarseMonoClock::now() + timeout;
}

} // namespace

//--------------------------------------------------------------------------------------------------
// PgCreateDatabase
//--------------------------------------------------------------------------------------------------

PgCreateDatabase::PgCreateDatabase(PgSession::ScopedRefPtr pg_session,
                                   const char *database_name,
                                   const PgOid database_oid,
                                   const PgOid source_database_oid,
                                   const PgOid next_oid,
                                   const bool colocated)
    : PgDdl(std::move(pg_session)),
      database_name_(database_name),
      database_oid_(database_oid),
      source_database_oid_(source_database_oid),
      next_oid_(next_oid),
      colocated_(colocated) {
}

PgCreateDatabase::~PgCreateDatabase() {
}

Status PgCreateDatabase::Exec() {
  boost::optional<TransactionMetadata> txn;
  if (txn_future_) {
    // Ensure the future has been executed by this time.
    txn = VERIFY_RESULT(Copy(txn_future_->get()));
  }
  return pg_session_->CreateDatabase(database_name_, database_oid_, source_database_oid_,
                                     next_oid_, txn, colocated_);
}

PgDropDatabase::PgDropDatabase(PgSession::ScopedRefPtr pg_session,
                               const char *database_name,
                               PgOid database_oid)
    : PgDdl(pg_session),
      database_name_(database_name),
      database_oid_(database_oid) {
}

PgDropDatabase::~PgDropDatabase() {
}

Status PgDropDatabase::Exec() {
  return pg_session_->DropDatabase(database_name_, database_oid_);
}

PgAlterDatabase::PgAlterDatabase(PgSession::ScopedRefPtr pg_session,
                               const char *database_name,
                               PgOid database_oid)
    : PgDdl(pg_session),
      namespace_alterer_(pg_session_->NewNamespaceAlterer(database_name, database_oid)) {
}

PgAlterDatabase::~PgAlterDatabase() {
  delete namespace_alterer_;
}

Status PgAlterDatabase::Exec() {
  return namespace_alterer_->SetDatabaseType(YQL_DATABASE_PGSQL)->Alter();
}

Status PgAlterDatabase::RenameDatabase(const char *newname) {
  namespace_alterer_->RenameTo(newname);
  return Status::OK();
}

//--------------------------------------------------------------------------------------------------
// PgCreateTablegroup / PgDropTablegroup
//--------------------------------------------------------------------------------------------------

PgCreateTablegroup::PgCreateTablegroup(PgSession::ScopedRefPtr pg_session,
                                       const char *database_name,
                                       const PgOid database_oid,
                                       const PgOid tablegroup_oid)
    : PgDdl(pg_session),
      database_name_(database_name),
      database_oid_(database_oid),
      tablegroup_oid_(tablegroup_oid) {
}

PgCreateTablegroup::~PgCreateTablegroup() {
}

Status PgCreateTablegroup::Exec() {
  Status s = pg_session_->CreateTablegroup(database_name_, database_oid_, tablegroup_oid_);

  if (PREDICT_FALSE(!s.ok())) {
    if (s.IsAlreadyPresent()) {
      return STATUS(InvalidArgument, "Duplicate tablegroup.");
    }
    if (s.IsNotFound()) {
      return STATUS(InvalidArgument, "Database not found", database_name_);
    }
    return STATUS_FORMAT(
        InvalidArgument, "Invalid table definition: $0",
        s.ToString(false /* include_file_and_line */, false /* include_code */));
  }

  return Status::OK();
}

PgDropTablegroup::PgDropTablegroup(PgSession::ScopedRefPtr pg_session,
                                   const PgOid database_oid,
                                   const PgOid tablegroup_oid)
    : PgDdl(pg_session),
      database_oid_(database_oid),
      tablegroup_oid_(tablegroup_oid) {
}

PgDropTablegroup::~PgDropTablegroup() {
}

Status PgDropTablegroup::Exec() {
  Status s = pg_session_->DropTablegroup(database_oid_, tablegroup_oid_);
  if (s.IsNotFound()) {
    return Status::OK();
  }
  return s;
}

//--------------------------------------------------------------------------------------------------
// PgCreateTable
//--------------------------------------------------------------------------------------------------

PgCreateTable::PgCreateTable(PgSession::ScopedRefPtr pg_session,
                             const char *database_name,
                             const char *schema_name,
                             const char *table_name,
                             const PgObjectId& table_id,
                             bool is_shared_table,
                             bool if_not_exist,
                             bool add_primary_key,
                             const bool colocated,
                             const PgObjectId& tablegroup_oid,
                             const PgObjectId& tablespace_oid)
    : PgDdl(pg_session) {
  table_id.ToPB(req_.mutable_table_id());
  req_.set_database_name(database_name);
  req_.set_table_name(table_name);
  req_.set_num_tablets(-1);
  req_.set_is_pg_catalog_table(strcmp(schema_name, "pg_catalog") == 0 ||
                               strcmp(schema_name, "information_schema") == 0);
  req_.set_is_shared_table(is_shared_table);
  req_.set_if_not_exist(if_not_exist);
  req_.set_colocated(colocated);
  tablegroup_oid.ToPB(req_.mutable_tablegroup_oid());
  tablespace_oid.ToPB(req_.mutable_tablespace_oid());

  // Add internal primary key column to a Postgres table without a user-specified primary key.
  if (add_primary_key) {
    // For regular user table, ybrowid should be a hash key because ybrowid is a random uuid.
    // For colocated or sys catalog table, ybrowid should be a range key because they are
    // unpartitioned tables in a single tablet.
    bool is_hash = !(req_.is_pg_catalog_table() || colocated || tablegroup_oid.IsValid());
    CHECK_OK(AddColumn("ybrowid", static_cast<int32_t>(PgSystemAttrNum::kYBRowId),
                       YB_YQL_DATA_TYPE_BINARY, is_hash, true /* is_range */));
  }
}

Status PgCreateTable::AddColumnImpl(const char *attr_name,
                                    int attr_num,
                                    int attr_ybtype,
                                    bool is_hash,
                                    bool is_range,
                                    ColumnSchema::SortingType sorting_type) {
  auto& column = *req_.mutable_create_columns()->Add();
  column.set_attr_name(attr_name);
  column.set_attr_num(attr_num);
  column.set_attr_ybtype(attr_ybtype);
  column.set_is_hash(is_hash);
  column.set_is_range(is_range);
  column.set_sorting_type(sorting_type);
  return Status::OK();
}

Status PgCreateTable::SetNumTablets(int32_t num_tablets) {
  if (num_tablets < 0) {
    return STATUS(InvalidArgument, "num_tablets cannot be less than zero");
  }
  if (num_tablets > FLAGS_max_num_tablets_for_table) {
    return STATUS(InvalidArgument, "num_tablets exceeds system limit");
  }

  req_.set_num_tablets(num_tablets);
  return Status::OK();
}

Status PgCreateTable::AddSplitBoundary(PgExpr **exprs, int expr_count) {
  auto* values = req_.mutable_split_bounds()->Add()->mutable_values();
  for (int i = 0; i < expr_count; ++i) {
    RETURN_NOT_OK(exprs[i]->Eval(values->Add()));
  }
  return Status::OK();
}

Status PgCreateTable::Exec() {
  RETURN_NOT_OK(pg_session_->pg_client().CreateTable(&req_, DdlDeadline()));
  auto base_table_id = PgObjectId::FromPB(req_.base_table_id());
  if (base_table_id.IsValid()) {
    pg_session_->InvalidateTableCache(base_table_id);
  }
  return Status::OK();
}

void PgCreateTable::SetupIndex(
    const PgObjectId& base_table_id, bool is_unique_index, bool skip_index_backfill) {
  base_table_id.ToPB(req_.mutable_base_table_id());
  req_.set_is_unique_index(is_unique_index);
  req_.set_skip_index_backfill(skip_index_backfill);
}

StmtOp PgCreateTable::stmt_op() const {
  return PgObjectId::FromPB(req_.base_table_id()).IsValid()
      ? StmtOp::STMT_CREATE_INDEX : StmtOp::STMT_CREATE_TABLE;
}

//--------------------------------------------------------------------------------------------------
// PgDropTable
//--------------------------------------------------------------------------------------------------

PgDropTable::PgDropTable(PgSession::ScopedRefPtr pg_session,
                         const PgObjectId& table_id,
                         bool if_exist)
    : PgDdl(pg_session),
      table_id_(table_id),
      if_exist_(if_exist) {
}

PgDropTable::~PgDropTable() {
}

Status PgDropTable::Exec() {
  Status s = pg_session_->DropTable(table_id_);
  pg_session_->InvalidateTableCache(table_id_);
  if (s.ok() || (s.IsNotFound() && if_exist_)) {
    return Status::OK();
  }
  return s;
}

//--------------------------------------------------------------------------------------------------
// PgTruncateTable
//--------------------------------------------------------------------------------------------------

PgTruncateTable::PgTruncateTable(PgSession::ScopedRefPtr pg_session,
                                 const PgObjectId& table_id)
    : PgDdl(pg_session),
      table_id_(table_id) {
}

PgTruncateTable::~PgTruncateTable() {
}

Status PgTruncateTable::Exec() {
  return pg_session_->TruncateTable(table_id_);
}

//--------------------------------------------------------------------------------------------------
// PgDropIndex
//--------------------------------------------------------------------------------------------------

PgDropIndex::PgDropIndex(PgSession::ScopedRefPtr pg_session,
                         const PgObjectId& index_id,
                         bool if_exist)
    : PgDropTable(pg_session, index_id, if_exist) {
}

PgDropIndex::~PgDropIndex() {
}

Status PgDropIndex::Exec() {
  client::YBTableName indexed_table_name;
  Status s = pg_session_->DropIndex(table_id_, &indexed_table_name);
  if (s.ok() || (s.IsNotFound() && if_exist_)) {
    RSTATUS_DCHECK(!indexed_table_name.empty(), Uninitialized, "indexed_table_name uninitialized");
    PgObjectId indexed_table_id(indexed_table_name.table_id());

    pg_session_->InvalidateTableCache(table_id_);
    pg_session_->InvalidateTableCache(indexed_table_id);
    return Status::OK();
  }
  return s;
}

//--------------------------------------------------------------------------------------------------
// PgAlterTable
//--------------------------------------------------------------------------------------------------

PgAlterTable::PgAlterTable(PgSession::ScopedRefPtr pg_session,
                           const PgObjectId& table_id)
    : PgDdl(pg_session) {
  table_id.ToPB(req_.mutable_table_id());
}

Status PgAlterTable::AddColumn(const char *name,
                               const YBCPgTypeEntity *attr_type,
                               int order) {
  auto& col = *req_.mutable_add_columns()->Add();
  col.set_attr_name(name);
  col.set_attr_ybtype(attr_type->yb_type);
  col.set_attr_num(order);

  return Status::OK();
}

Status PgAlterTable::RenameColumn(const char *oldname, const char *newname) {
  auto& rename = *req_.mutable_rename_columns()->Add();
  rename.set_old_name(oldname);
  rename.set_new_name(newname);
  return Status::OK();
}

Status PgAlterTable::DropColumn(const char *name) {
  req_.mutable_drop_columns()->Add(name);
  return Status::OK();
}

Status PgAlterTable::RenameTable(const char *db_name, const char *newname) {
  auto& rename = *req_.mutable_rename_table();
  rename.set_database_name(db_name);
  rename.set_table_name(newname);
  return Status::OK();
}

Status PgAlterTable::Exec() {
  RETURN_NOT_OK(pg_session_->pg_client().AlterTable(&req_, DdlDeadline()));
  pg_session_->InvalidateTableCache(PgObjectId::FromPB(req_.table_id()));
  return Status::OK();
}

PgAlterTable::~PgAlterTable() {
}

}  // namespace pggate
}  // namespace yb
