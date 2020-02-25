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
//--------------------------------------------------------------------------------------------------

#include <boost/optional.hpp>

#include "yb/client/yb_table_name.h"

#include "yb/yql/pggate/pggate.h"
#include "yb/yql/pggate/pggate_flags.h"
#include "yb/yql/pggate/pg_ddl.h"
#include "yb/yql/pggate/pg_insert.h"
#include "yb/yql/pggate/pg_update.h"
#include "yb/yql/pggate/pg_delete.h"
#include "yb/yql/pggate/pg_truncate_colocated.h"
#include "yb/yql/pggate/pg_select.h"
#include "yb/yql/pggate/pg_txn_manager.h"
#include "yb/yql/pggate/ybc_pggate.h"

#include "yb/util/flag_tags.h"
#include "yb/client/client_fwd.h"
#include "yb/client/client_utils.h"
#include "yb/rpc/messenger.h"
#include "yb/rpc/secure_stream.h"
#include "yb/server/secure.h"

#include "yb/tserver/tserver_shared_mem.h"

DECLARE_string(rpc_bind_addresses);
DECLARE_bool(use_node_to_node_encryption);
DECLARE_string(certs_dir);

namespace yb {
namespace pggate {

namespace {

CHECKED_STATUS AddColumn(PgCreateTable* pg_stmt, const char *attr_name, int attr_num,
                         const YBCPgTypeEntity *attr_type, bool is_hash, bool is_range,
                         bool is_desc, bool is_nulls_first) {
  using SortingType = ColumnSchema::SortingType;
  SortingType sorting_type = SortingType::kNotSpecified;

  if (!is_hash && is_range) {
    if (is_desc) {
      sorting_type = is_nulls_first ? SortingType::kDescending : SortingType::kDescendingNullsLast;
    } else {
      sorting_type = is_nulls_first ? SortingType::kAscending : SortingType::kAscendingNullsLast;
    }
  }

  return pg_stmt->AddColumn(attr_name, attr_num, attr_type, is_hash, is_range, sorting_type);
}

Result<PgApiImpl::MessengerHolder> BuildMessenger(
    const string& client_name,
    int32_t num_reactors,
    const scoped_refptr<MetricEntity>& metric_entity,
    const std::shared_ptr<MemTracker>& parent_mem_tracker) {
  std::unique_ptr<rpc::SecureContext> secure_context;
  if (FLAGS_use_node_to_node_encryption) {
    secure_context = VERIFY_RESULT(server::CreateSecureContext(FLAGS_certs_dir));
  }
  auto messenger = VERIFY_RESULT(client::CreateClientMessenger(
      client_name, num_reactors, metric_entity, parent_mem_tracker, secure_context.get()));
  return PgApiImpl::MessengerHolder{std::move(secure_context), std::move(messenger)};
}

std::unique_ptr<tserver::TServerSharedObject> InitTServerSharedObject() {
  // Do not use shared memory in initdb or if explicity set to be ignored.
  if (YBCIsInitDbModeEnvVarSet() || FLAGS_pggate_ignore_tserver_shm ||
      FLAGS_pggate_tserver_shm_fd == -1) {
    return nullptr;
  }
  return std::make_unique<tserver::TServerSharedObject>(CHECK_RESULT(
      tserver::TServerSharedObject::OpenReadOnly(FLAGS_pggate_tserver_shm_fd)));
}

} // namespace

using std::make_shared;
using client::YBSession;

//--------------------------------------------------------------------------------------------------

PggateOptions::PggateOptions() {
  server_type = "tserver";
  rpc_opts.default_port = kDefaultPort;
  rpc_opts.connection_keepalive_time_ms = FLAGS_pgsql_rpc_keepalive_time_ms;

  if (FLAGS_pggate_proxy_bind_address.empty()) {
    HostPort host_port;
    CHECK_OK(host_port.ParseString(FLAGS_rpc_bind_addresses, 0));
    host_port.set_port(PggateOptions::kDefaultPort);
    FLAGS_pggate_proxy_bind_address = host_port.ToString();
    LOG(INFO) << "Reset YSQL bind address to " << FLAGS_pggate_proxy_bind_address;
  }
  rpc_opts.rpc_bind_addresses = FLAGS_pggate_proxy_bind_address;
  master_addresses_flag = FLAGS_pggate_master_addresses;

  server::MasterAddresses master_addresses;
  // TODO: we might have to allow setting master_replication_factor similarly to how it is done
  // in tserver to support master auto-discovery on Kubernetes.
  CHECK_OK(server::DetermineMasterAddresses(
      "pggate_master_addresses", master_addresses_flag, /* master_replication_factor */ 0,
      &master_addresses, &master_addresses_flag));
  SetMasterAddresses(make_shared<server::MasterAddresses>(std::move(master_addresses)));
}

//--------------------------------------------------------------------------------------------------

PgApiImpl::PgApiImpl(const YBCPgTypeEntity *YBCDataTypeArray, int count, YBCPgCallbacks callbacks)
    : metric_registry_(new MetricRegistry()),
      metric_entity_(METRIC_ENTITY_server.Instantiate(metric_registry_.get(), "yb.pggate")),
      mem_tracker_(MemTracker::CreateTracker("PostgreSQL")),
      messenger_holder_(CHECK_RESULT(BuildMessenger("pggate_ybclient",
                                                    FLAGS_pggate_ybclient_reactor_threads,
                                                    metric_entity_,
                                                    mem_tracker_))),
      async_client_init_(messenger_holder_.messenger.get()->name(),
                         FLAGS_pggate_ybclient_reactor_threads,
                         FLAGS_pggate_rpc_timeout_secs,
                         "" /* tserver_uuid */,
                         &pggate_options_,
                         metric_entity_,
                         mem_tracker_,
                         messenger_holder_.messenger.get()),
      clock_(new server::HybridClock()),
      tserver_shared_object_(InitTServerSharedObject()),
      pg_txn_manager_(new PgTxnManager(&async_client_init_, clock_, tserver_shared_object_.get())),
      pg_callbacks_(callbacks) {
  CHECK_OK(clock_->Init());

  // Setup type mapping.
  for (int idx = 0; idx < count; idx++) {
    const YBCPgTypeEntity *type_entity = &YBCDataTypeArray[idx];
    type_map_[type_entity->type_oid] = type_entity;
  }

  async_client_init_.Start();
}

PgApiImpl::~PgApiImpl() {
  messenger_holder_.messenger->Shutdown();
}

const YBCPgTypeEntity *PgApiImpl::FindTypeEntity(int type_oid) {
  const auto iter = type_map_.find(type_oid);
  if (iter != type_map_.end()) {
    return iter->second;
  }
  return nullptr;
}

//--------------------------------------------------------------------------------------------------

Status PgApiImpl::CreateEnv(PgEnv **pg_env) {
  *pg_env = pg_env_.get();
  return Status::OK();
}

Status PgApiImpl::DestroyEnv(PgEnv *pg_env) {
  pg_env_ = nullptr;
  return Status::OK();
}

//--------------------------------------------------------------------------------------------------

Status PgApiImpl::InitSession(const PgEnv *pg_env,
                              const string& database_name) {
  CHECK(!pg_session_);
  auto session = make_scoped_refptr<PgSession>(client(),
                                               database_name,
                                               pg_txn_manager_,
                                               clock_,
                                               tserver_shared_object_.get(),
                                               pg_callbacks_);
  if (!database_name.empty()) {
    RETURN_NOT_OK(session->ConnectDatabase(database_name));
  }

  pg_session_.swap(session);
  return Status::OK();
}

Status PgApiImpl::InvalidateCache() {
  pg_session_->InvalidateCache();
  return Status::OK();
}

//--------------------------------------------------------------------------------------------------

Status PgApiImpl::CreateSequencesDataTable() {
  return pg_session_->CreateSequencesDataTable();
}

Status PgApiImpl::InsertSequenceTuple(int64_t db_oid,
                                      int64_t seq_oid,
                                      uint64_t ysql_catalog_version,
                                      int64_t last_val,
                                      bool is_called) {
  return pg_session_->InsertSequenceTuple(
      db_oid, seq_oid, ysql_catalog_version, last_val, is_called);
}

Status PgApiImpl::UpdateSequenceTupleConditionally(int64_t db_oid,
                                                   int64_t seq_oid,
                                                   uint64_t ysql_catalog_version,
                                                   int64_t last_val,
                                                   bool is_called,
                                                   int64_t expected_last_val,
                                                   bool expected_is_called,
                                                   bool *skipped) {
  return pg_session_->UpdateSequenceTuple(
      db_oid, seq_oid, ysql_catalog_version, last_val, is_called,
      expected_last_val, expected_is_called, skipped);
}

Status PgApiImpl::UpdateSequenceTuple(int64_t db_oid,
                                      int64_t seq_oid,
                                      uint64_t ysql_catalog_version,
                                      int64_t last_val,
                                      bool is_called,
                                      bool* skipped) {
  return pg_session_->UpdateSequenceTuple(
      db_oid, seq_oid, ysql_catalog_version, last_val,
      is_called, boost::none, boost::none, skipped);
}

Status PgApiImpl::ReadSequenceTuple(int64_t db_oid,
                                    int64_t seq_oid,
                                    uint64_t ysql_catalog_version,
                                    int64_t *last_val,
                                    bool *is_called) {
  return pg_session_->ReadSequenceTuple(db_oid, seq_oid, ysql_catalog_version, last_val, is_called);
}

Status PgApiImpl::DeleteSequenceTuple(int64_t db_oid, int64_t seq_oid) {
  return pg_session_->DeleteSequenceTuple(db_oid, seq_oid);
}


//--------------------------------------------------------------------------------------------------

Status PgApiImpl::DeleteStatement(PgStatement *handle) {
  if (handle) {
    handle->Release();
  }
  return Status::OK();
}

Status PgApiImpl::ClearBinds(PgStatement *handle) {
  return handle->ClearBinds();
}

//--------------------------------------------------------------------------------------------------

Status PgApiImpl::ConnectDatabase(const char *database_name) {
  return pg_session_->ConnectDatabase(database_name);
}

Status PgApiImpl::NewCreateDatabase(const char *database_name,
                                    const PgOid database_oid,
                                    const PgOid source_database_oid,
                                    const PgOid next_oid,
                                    const bool colocated,
                                    PgStatement **handle) {
  auto stmt = make_scoped_refptr<PgCreateDatabase>(pg_session_, database_name, database_oid,
                                                   source_database_oid, next_oid, colocated);
  *handle = stmt.detach();
  return Status::OK();
}

Status PgApiImpl::ExecCreateDatabase(PgStatement *handle) {
  if (!PgStatement::IsValidStmt(handle, StmtOp::STMT_CREATE_DATABASE)) {
    // Invalid handle.
    return STATUS(InvalidArgument, "Invalid statement handle");
  }

  return down_cast<PgCreateDatabase*>(handle)->Exec();
}

Status PgApiImpl::NewDropDatabase(const char *database_name,
                                  PgOid database_oid,
                                  PgStatement **handle) {
  auto stmt = make_scoped_refptr<PgDropDatabase>(pg_session_, database_name, database_oid);
  *handle = stmt.detach();
  return Status::OK();
}

Status PgApiImpl::ExecDropDatabase(PgStatement *handle) {
  if (!PgStatement::IsValidStmt(handle, StmtOp::STMT_DROP_DATABASE)) {
    // Invalid handle.
    return STATUS(InvalidArgument, "Invalid statement handle");
  }
  return down_cast<PgDropDatabase*>(handle)->Exec();
}

Status PgApiImpl::NewAlterDatabase(const char *database_name,
                                  PgOid database_oid,
                                  PgStatement **handle) {
  auto stmt = make_scoped_refptr<PgAlterDatabase>(pg_session_, database_name, database_oid);
  *handle = stmt.detach();
  return Status::OK();
}

Status PgApiImpl::AlterDatabaseRenameDatabase(PgStatement *handle, const char *newname) {
  if (!PgStatement::IsValidStmt(handle, StmtOp::STMT_ALTER_DATABASE)) {
    // Invalid handle.
    return STATUS(InvalidArgument, "Invalid statement handle");
  }
  return down_cast<PgAlterDatabase*>(handle)->RenameDatabase(newname);
}

Status PgApiImpl::ExecAlterDatabase(PgStatement *handle) {
  if (!PgStatement::IsValidStmt(handle, StmtOp::STMT_ALTER_DATABASE)) {
    // Invalid handle.
    return STATUS(InvalidArgument, "Invalid statement handle");
  }
  return down_cast<PgAlterDatabase*>(handle)->Exec();
}

Status PgApiImpl::ReserveOids(const PgOid database_oid,
                              const PgOid next_oid,
                              const uint32_t count,
                              PgOid *begin_oid,
                              PgOid *end_oid) {
  return pg_session_->ReserveOids(database_oid, next_oid, count, begin_oid, end_oid);
}

Status PgApiImpl::GetCatalogMasterVersion(uint64_t *version) {
  return pg_session_->GetCatalogMasterVersion(version);
}

//--------------------------------------------------------------------------------------------------

Status PgApiImpl::NewCreateTable(const char *database_name,
                                 const char *schema_name,
                                 const char *table_name,
                                 const PgObjectId& table_id,
                                 bool is_shared_table,
                                 bool if_not_exist,
                                 bool add_primary_key,
                                 PgStatement **handle) {
  auto stmt = make_scoped_refptr<PgCreateTable>(
      pg_session_, database_name, schema_name, table_name,
      table_id, is_shared_table, if_not_exist, add_primary_key);
  *handle = stmt.detach();
  return Status::OK();
}

Status PgApiImpl::CreateTableAddColumn(PgStatement *handle, const char *attr_name, int attr_num,
                                       const YBCPgTypeEntity *attr_type,
                                       bool is_hash, bool is_range,
                                       bool is_desc, bool is_nulls_first) {
  if (!PgStatement::IsValidStmt(handle, StmtOp::STMT_CREATE_TABLE)) {
    // Invalid handle.
    return STATUS(InvalidArgument, "Invalid statement handle");
  }
  return AddColumn(down_cast<PgCreateTable*>(handle), attr_name, attr_num, attr_type,
      is_hash, is_range, is_desc, is_nulls_first);
}

Status PgApiImpl::CreateTableSetNumTablets(PgStatement *handle, int32_t num_tablets) {
  if (!PgStatement::IsValidStmt(handle, StmtOp::STMT_CREATE_TABLE)) {
    // Invalid handle.
    return STATUS(InvalidArgument, "Invalid statement handle");
  }
  return down_cast<PgCreateTable*>(handle)->SetNumTablets(num_tablets);
}

Status PgApiImpl::CreateTableSetColocated(PgStatement *handle, bool colocated) {
  if (!PgStatement::IsValidStmt(handle, StmtOp::STMT_CREATE_TABLE)) {
    // Invalid handle.
    return STATUS(InvalidArgument, "Invalid statement handle");
  }
  down_cast<PgCreateTable*>(handle)->SetColocated(colocated);
  return Status::OK();
}

Status PgApiImpl::ExecCreateTable(PgStatement *handle) {
  if (!PgStatement::IsValidStmt(handle, StmtOp::STMT_CREATE_TABLE)) {
    // Invalid handle.
    return STATUS(InvalidArgument, "Invalid statement handle");
  }
  return down_cast<PgCreateTable*>(handle)->Exec();
}

Status PgApiImpl::NewAlterTable(const PgObjectId& table_id,
                                PgStatement **handle) {
  auto stmt = make_scoped_refptr<PgAlterTable>(pg_session_, table_id);
  *handle = stmt.detach();
  return Status::OK();
}

Status PgApiImpl::AlterTableAddColumn(PgStatement *handle, const char *name,
                                      int order, const YBCPgTypeEntity *attr_type,
                                      bool is_not_null) {
  if (!PgStatement::IsValidStmt(handle, StmtOp::STMT_ALTER_TABLE)) {
    // Invalid handle.
    return STATUS(InvalidArgument, "Invalid statement handle");
  }

  PgAlterTable *pg_stmt = down_cast<PgAlterTable*>(handle);
  return pg_stmt->AddColumn(name, attr_type, order, is_not_null);
}

Status PgApiImpl::AlterTableRenameColumn(PgStatement *handle, const char *oldname,
                                         const char *newname) {
  if (!PgStatement::IsValidStmt(handle, StmtOp::STMT_ALTER_TABLE)) {
    // Invalid handle.
    return STATUS(InvalidArgument, "Invalid statement handle");
  }

  PgAlterTable *pg_stmt = down_cast<PgAlterTable*>(handle);
  return pg_stmt->RenameColumn(oldname, newname);
}

Status PgApiImpl::AlterTableDropColumn(PgStatement *handle, const char *name) {
  if (!PgStatement::IsValidStmt(handle, StmtOp::STMT_ALTER_TABLE)) {
    // Invalid handle.
    return STATUS(InvalidArgument, "Invalid statement handle");
  }

  PgAlterTable *pg_stmt = down_cast<PgAlterTable*>(handle);
  return pg_stmt->DropColumn(name);
}

Status PgApiImpl::AlterTableRenameTable(PgStatement *handle, const char *db_name,
                                        const char *newname) {
  if (!PgStatement::IsValidStmt(handle, StmtOp::STMT_ALTER_TABLE)) {
    // Invalid handle.
    return STATUS(InvalidArgument, "Invalid statement handle");
  }

  PgAlterTable *pg_stmt = down_cast<PgAlterTable*>(handle);
  return pg_stmt->RenameTable(db_name, newname);
}

Status PgApiImpl::ExecAlterTable(PgStatement *handle) {
  if (!PgStatement::IsValidStmt(handle, StmtOp::STMT_ALTER_TABLE)) {
    // Invalid handle.
    return STATUS(InvalidArgument, "Invalid statement handle");
  }
  PgAlterTable *pg_stmt = down_cast<PgAlterTable*>(handle);
  return pg_stmt->Exec();
}

Status PgApiImpl::NewDropTable(const PgObjectId& table_id,
                               bool if_exist,
                               PgStatement **handle) {
  auto stmt = make_scoped_refptr<PgDropTable>(pg_session_, table_id, if_exist);
  *handle = stmt.detach();
  return Status::OK();
}

Status PgApiImpl::ExecDropTable(PgStatement *handle) {
  if (!PgStatement::IsValidStmt(handle, StmtOp::STMT_DROP_TABLE)) {
    // Invalid handle.
    return STATUS(InvalidArgument, "Invalid statement handle");
  }
  return down_cast<PgDropTable*>(handle)->Exec();
}

Status PgApiImpl::NewTruncateTable(const PgObjectId& table_id,
                                   PgStatement **handle) {
  auto stmt = make_scoped_refptr<PgTruncateTable>(pg_session_, table_id);
  *handle = stmt.detach();
  return Status::OK();
}

Status PgApiImpl::ExecTruncateTable(PgStatement *handle) {
  if (!PgStatement::IsValidStmt(handle, StmtOp::STMT_TRUNCATE_TABLE)) {
    // Invalid handle.
    return STATUS(InvalidArgument, "Invalid statement handle");
  }
  return down_cast<PgTruncateTable*>(handle)->Exec();
}

Status PgApiImpl::GetTableDesc(const PgObjectId& table_id,
                               PgTableDesc **handle) {
  PgTableDesc::ScopedRefPtr table;
  auto result = pg_session_->LoadTable(table_id);
  RETURN_NOT_OK(result);
  *handle = (*result).detach();
  return Status::OK();
}

Status PgApiImpl::DeleteTableDesc(PgTableDesc *handle) {
  if (handle) {
    handle->Release();
  }
  return Status::OK();
}

Status PgApiImpl::GetColumnInfo(YBCPgTableDesc table_desc,
                                int16_t attr_number,
                                bool *is_primary,
                                bool *is_hash) {
  return table_desc->GetColumnInfo(attr_number, is_primary, is_hash);
}

Status PgApiImpl::DmlModifiesRow(PgStatement *handle, bool *modifies_row) {
  if (!handle) {
    return STATUS(InvalidArgument, "Invalid statement handle");
  }

  *modifies_row = false;

  switch (handle->stmt_op()) {
    case StmtOp::STMT_UPDATE:
    case StmtOp::STMT_DELETE:
      *modifies_row = true;
      break;
    default:
      break;
  }

  return Status::OK();
}

Status PgApiImpl::SetIsSysCatalogVersionChange(PgStatement *handle) {
  if (!handle) {
    return STATUS(InvalidArgument, "Invalid statement handle");
  }

  switch (handle->stmt_op()) {
    case StmtOp::STMT_UPDATE:
    case StmtOp::STMT_DELETE:
    case StmtOp::STMT_INSERT:
      down_cast<PgDmlWrite *>(handle)->SetIsSystemCatalogChange();
      return Status::OK();
    default:
      break;
  }

  return STATUS(InvalidArgument, "Invalid statement handle");
}

Status PgApiImpl::SetCatalogCacheVersion(PgStatement *handle, uint64_t catalog_cache_version) {
  if (!handle) {
    return STATUS(InvalidArgument, "Invalid statement handle");
  }

  switch (handle->stmt_op()) {
    case StmtOp::STMT_SELECT:
    case StmtOp::STMT_INSERT:
    case StmtOp::STMT_UPDATE:
    case StmtOp::STMT_DELETE:
      down_cast<PgDml *>(handle)->SetCatalogCacheVersion(catalog_cache_version);
      return Status::OK();
    default:
      break;
  }

  return STATUS(InvalidArgument, "Invalid statement handle");
}

//--------------------------------------------------------------------------------------------------

Status PgApiImpl::NewCreateIndex(const char *database_name,
                                 const char *schema_name,
                                 const char *index_name,
                                 const PgObjectId& index_id,
                                 const PgObjectId& base_table_id,
                                 bool is_shared_index,
                                 bool is_unique_index,
                                 bool if_not_exist,
                                 PgStatement **handle) {
  auto stmt = make_scoped_refptr<PgCreateIndex>(
      pg_session_, database_name, schema_name, index_name, index_id, base_table_id,
      is_shared_index, is_unique_index, if_not_exist);
  *handle = stmt.detach();
  return Status::OK();
}

Status PgApiImpl::CreateIndexAddColumn(PgStatement *handle, const char *attr_name, int attr_num,
                                       const YBCPgTypeEntity *attr_type,
                                       bool is_hash, bool is_range,
                                       bool is_desc, bool is_nulls_first) {
  if (!PgStatement::IsValidStmt(handle, StmtOp::STMT_CREATE_INDEX)) {
    // Invalid handle.
    return STATUS(InvalidArgument, "Invalid statement handle");
  }

  return AddColumn(down_cast<PgCreateIndex*>(handle), attr_name, attr_num, attr_type,
      is_hash, is_range, is_desc, is_nulls_first);
}

Status PgApiImpl::ExecCreateIndex(PgStatement *handle) {
  if (!PgStatement::IsValidStmt(handle, StmtOp::STMT_CREATE_INDEX)) {
    // Invalid handle.
    return STATUS(InvalidArgument, "Invalid statement handle");
  }
  return down_cast<PgCreateIndex*>(handle)->Exec();
}

Status PgApiImpl::NewDropIndex(const PgObjectId& index_id,
                               bool if_exist,
                               PgStatement **handle) {
  auto stmt = make_scoped_refptr<PgDropIndex>(pg_session_, index_id, if_exist);
  *handle = stmt.detach();
  return Status::OK();
}

Status PgApiImpl::ExecDropIndex(PgStatement *handle) {
  if (!PgStatement::IsValidStmt(handle, StmtOp::STMT_DROP_INDEX)) {
    // Invalid handle.
    return STATUS(InvalidArgument, "Invalid statement handle");
  }
  return down_cast<PgDropIndex*>(handle)->Exec();
}

//--------------------------------------------------------------------------------------------------
// DML Statment Support.
//--------------------------------------------------------------------------------------------------

// Binding -----------------------------------------------------------------------------------------

Status PgApiImpl::DmlAppendTarget(PgStatement *handle, PgExpr *target) {
  return down_cast<PgDml*>(handle)->AppendTarget(target);
}

Status PgApiImpl::DmlBindColumn(PgStatement *handle, int attr_num, PgExpr *attr_value) {
  return down_cast<PgDml*>(handle)->BindColumn(attr_num, attr_value);
}

Status PgApiImpl::DmlBindColumnCondEq(PgStatement *handle, int attr_num, PgExpr *attr_value) {
  return down_cast<PgDmlRead*>(handle)->BindColumnCondEq(attr_num, attr_value);
}

Status PgApiImpl::DmlBindColumnCondBetween(PgStatement *handle, int attr_num, PgExpr *attr_value,
    PgExpr *attr_value_end) {
  return down_cast<PgDmlRead*>(handle)->BindColumnCondBetween(attr_num, attr_value, attr_value_end);
}

Status PgApiImpl::DmlBindColumnCondIn(PgStatement *handle, int attr_num, int n_attr_values,
    PgExpr **attr_values) {
  return down_cast<PgDmlRead*>(handle)->BindColumnCondIn(attr_num, n_attr_values, attr_values);
}

Status PgApiImpl::DmlBindTable(PgStatement *handle) {
  return down_cast<PgDml*>(handle)->BindTable();
}

CHECKED_STATUS PgApiImpl::DmlAssignColumn(PgStatement *handle, int attr_num, PgExpr *attr_value) {
  return down_cast<PgDml*>(handle)->AssignColumn(attr_num, attr_value);
}

Status PgApiImpl::DmlFetch(PgStatement *handle, int32_t natts, uint64_t *values, bool *isnulls,
                           PgSysColumns *syscols, bool *has_data) {
  return down_cast<PgDml*>(handle)->Fetch(natts, values, isnulls, syscols, has_data);
}

Status PgApiImpl::DmlBuildYBTupleId(PgStatement *handle, const PgAttrValueDescriptor *attrs,
                                    int32_t nattrs, uint64_t *ybctid) {
  const string id = VERIFY_RESULT(down_cast<PgDml*>(handle)->BuildYBTupleId(attrs, nattrs));
  const YBCPgTypeEntity *type_entity = FindTypeEntity(kPgByteArrayOid);
  *ybctid = type_entity->yb_to_datum(id.data(), id.size(), nullptr /* type_attrs */);
  return Status::OK();
}

Status PgApiImpl::StartOperationsBuffering() {
  return pg_session_->StartOperationsBuffering();
}

Status PgApiImpl::FlushBufferedOperations() {
  return pg_session_->FlushBufferedOperations();
}

Status PgApiImpl::DmlExecWriteOp(PgStatement *handle, int32_t *rows_affected_count) {
  switch (handle->stmt_op()) {
    case StmtOp::STMT_INSERT:
    case StmtOp::STMT_UPDATE:
    case StmtOp::STMT_DELETE:
    case StmtOp::STMT_TRUNCATE:
      {
        auto dml_write = down_cast<PgDmlWrite *>(handle);
        RETURN_NOT_OK(dml_write->Exec(rows_affected_count != nullptr /* force_non_bufferable */));
        if (rows_affected_count) {
          *rows_affected_count = dml_write->GetRowsAffectedCount();
        }
        return Status::OK();
      }
    default:
      break;
  }
  return STATUS(InvalidArgument, "Invalid statement handle");
}

// Insert ------------------------------------------------------------------------------------------

Status PgApiImpl::NewInsert(const PgObjectId& table_id,
                            const bool is_single_row_txn,
                            PgStatement **handle) {
  *handle = nullptr;
  auto stmt = make_scoped_refptr<PgInsert>(pg_session_, table_id, is_single_row_txn);
  RETURN_NOT_OK(stmt->Prepare());
  *handle = stmt.detach();
  return Status::OK();
}

Status PgApiImpl::ExecInsert(PgStatement *handle) {
  if (!PgStatement::IsValidStmt(handle, StmtOp::STMT_INSERT)) {
    // Invalid handle.
    return STATUS(InvalidArgument, "Invalid statement handle");
  }
  return down_cast<PgInsert*>(handle)->Exec();
}

// Update ------------------------------------------------------------------------------------------

Status PgApiImpl::NewUpdate(const PgObjectId& table_id,
                            const bool is_single_row_txn,
                            PgStatement **handle) {
  *handle = nullptr;
  auto stmt = make_scoped_refptr<PgUpdate>(pg_session_, table_id, is_single_row_txn);
  RETURN_NOT_OK(stmt->Prepare());
  *handle = stmt.detach();
  return Status::OK();
}

Status PgApiImpl::ExecUpdate(PgStatement *handle) {
  if (!PgStatement::IsValidStmt(handle, StmtOp::STMT_UPDATE)) {
    // Invalid handle.
    return STATUS(InvalidArgument, "Invalid statement handle");
  }
  return down_cast<PgUpdate*>(handle)->Exec();
}

// Delete ------------------------------------------------------------------------------------------

Status PgApiImpl::NewDelete(const PgObjectId& table_id,
                            const bool is_single_row_txn,
                            PgStatement **handle) {
  *handle = nullptr;
  auto stmt = make_scoped_refptr<PgDelete>(pg_session_, table_id, is_single_row_txn);
  RETURN_NOT_OK(stmt->Prepare());
  *handle = stmt.detach();
  return Status::OK();
}

Status PgApiImpl::ExecDelete(PgStatement *handle) {
  if (!PgStatement::IsValidStmt(handle, StmtOp::STMT_DELETE)) {
    // Invalid handle.
    return STATUS(InvalidArgument, "Invalid statement handle");
  }
  return down_cast<PgDelete*>(handle)->Exec();
}

// Colocated Truncate ------------------------------------------------------------------------------

Status PgApiImpl::NewTruncateColocated(const PgObjectId& table_id,
                                       const bool is_single_row_txn,
                                       PgStatement **handle) {
  *handle = nullptr;
  auto stmt = make_scoped_refptr<PgTruncateColocated>(pg_session_, table_id, is_single_row_txn);
  RETURN_NOT_OK(stmt->Prepare());
  *handle = stmt.detach();
  return Status::OK();
}

Status PgApiImpl::ExecTruncateColocated(PgStatement *handle) {
  if (!PgStatement::IsValidStmt(handle, StmtOp::STMT_TRUNCATE)) {
    // Invalid handle.
    return STATUS(InvalidArgument, "Invalid statement handle");
  }
  return down_cast<PgTruncateColocated*>(handle)->Exec();
}

// Select ------------------------------------------------------------------------------------------

Status PgApiImpl::NewSelect(const PgObjectId& table_id,
                            const PgObjectId& index_id,
                            const PgPrepareParameters *prepare_params,
                            PgStatement **handle) {
  // Scenarios:
  // - Sequential Scan: PgSelect to read from table_id.
  // - Primary Scan: PgSelect from table_id. YugaByte does not have separate table for primary key.
  // - Index-Only-Scan: PgSelectIndex directly from secondary index_id.
  // - IndexScan: Use PgSelectIndex to read from index_id and then PgSelect to read from table_id.
  //     Note that for SysTable, only one request is send for both table_id and index_id.
  *handle = nullptr;
  PgDmlRead::ScopedRefPtr stmt;
  if (prepare_params && prepare_params->index_only_scan && prepare_params->use_secondary_index) {
    if (!index_id.IsValid()) {
      return STATUS(InvalidArgument, "Cannot run query with invalid index ID");
    }
    stmt = make_scoped_refptr<PgSelectIndex>(pg_session_, table_id, index_id, prepare_params);
  } else {
    // For IndexScan PgSelect processing will create subquery PgSelectIndex.
    stmt = make_scoped_refptr<PgSelect>(pg_session_, table_id, index_id, prepare_params);
  }

  RETURN_NOT_OK(stmt->Prepare());
  *handle = stmt.detach();
  return Status::OK();
}

Status PgApiImpl::SetForwardScan(PgStatement *handle, bool is_forward_scan) {
  if (!PgStatement::IsValidStmt(handle, StmtOp::STMT_SELECT)) {
    // Invalid handle.
    return STATUS(InvalidArgument, "Invalid statement handle");
  }
  down_cast<PgDmlRead*>(handle)->SetForwardScan(is_forward_scan);
  return Status::OK();
}

Status PgApiImpl::ExecSelect(PgStatement *handle, const PgExecParameters *exec_params) {
  if (!PgStatement::IsValidStmt(handle, StmtOp::STMT_SELECT)) {
    // Invalid handle.
    return STATUS(InvalidArgument, "Invalid statement handle");
  }
  return down_cast<PgDmlRead*>(handle)->Exec(exec_params);
}

//--------------------------------------------------------------------------------------------------
// Expressions.
//--------------------------------------------------------------------------------------------------

// Column references -------------------------------------------------------------------------------

Status PgApiImpl::NewColumnRef(PgStatement *stmt, int attr_num, const PgTypeEntity *type_entity,
                               const PgTypeAttrs *type_attrs, PgExpr **expr_handle) {
  if (!stmt) {
    // Invalid handle.
    return STATUS(InvalidArgument, "Invalid statement handle");
  }
  PgColumnRef::SharedPtr colref = make_shared<PgColumnRef>(attr_num, type_entity, type_attrs);
  stmt->AddExpr(colref);

  *expr_handle = colref.get();
  return Status::OK();
}

// Constant ----------------------------------------------------------------------------------------
Status PgApiImpl::NewConstant(YBCPgStatement stmt, const YBCPgTypeEntity *type_entity,
                              uint64_t datum, bool is_null, YBCPgExpr *expr_handle) {
  if (!stmt) {
    // Invalid handle.
    return STATUS(InvalidArgument, "Invalid statement handle");
  }
  PgExpr::SharedPtr pg_const = make_shared<PgConstant>(type_entity, datum, is_null);
  stmt->AddExpr(pg_const);

  *expr_handle = pg_const.get();
  return Status::OK();
}

Status PgApiImpl::NewConstantOp(YBCPgStatement stmt, const YBCPgTypeEntity *type_entity,
                              uint64_t datum, bool is_null, YBCPgExpr *expr_handle, bool is_gt) {
  if (!stmt) {
    // Invalid handle.
    return STATUS(InvalidArgument, "Invalid statement handle");
  }
  PgExpr::SharedPtr pg_const = make_shared<PgConstant>(type_entity, datum, is_null,
      is_gt ? PgExpr::Opcode::PG_EXPR_GT : PgExpr::Opcode::PG_EXPR_LT);
  stmt->AddExpr(pg_const);

  *expr_handle = pg_const.get();
  return Status::OK();
}

// Text constant -----------------------------------------------------------------------------------

Status PgApiImpl::UpdateConstant(PgExpr *expr, const char *value, bool is_null) {
  if (expr->opcode() != PgExpr::Opcode::PG_EXPR_CONSTANT) {
    // Invalid handle.
    return STATUS(InvalidArgument, "Invalid expression handle for constant");
  }
  down_cast<PgConstant*>(expr)->UpdateConstant(value, is_null);
  return Status::OK();
}

Status PgApiImpl::UpdateConstant(PgExpr *expr, const void *value, int64_t bytes, bool is_null) {
  if (expr->opcode() != PgExpr::Opcode::PG_EXPR_CONSTANT) {
    // Invalid handle.
    return STATUS(InvalidArgument, "Invalid expression handle for constant");
  }
  down_cast<PgConstant*>(expr)->UpdateConstant(value, bytes, is_null);
  return Status::OK();
}

// Text constant -----------------------------------------------------------------------------------

Status PgApiImpl::NewOperator(PgStatement *stmt, const char *opname,
                              const YBCPgTypeEntity *type_entity,
                              PgExpr **op_handle) {
  if (!stmt) {
    // Invalid handle.
    return STATUS(InvalidArgument, "Invalid statement handle");
  }
  RETURN_NOT_OK(PgExpr::CheckOperatorName(opname));

  // Create operator.
  PgExpr::SharedPtr pg_op = make_shared<PgOperator>(opname, type_entity);
  stmt->AddExpr(pg_op);

  *op_handle = pg_op.get();
  return Status::OK();
}

Status PgApiImpl::OperatorAppendArg(PgExpr *op_handle, PgExpr *arg) {
  if (!op_handle || !arg) {
    // Invalid handle.
    return STATUS(InvalidArgument, "Invalid expression handle");
  }
  down_cast<PgOperator*>(op_handle)->AppendArg(arg);
  return Status::OK();
}

Result<bool> PgApiImpl::IsInitDbDone() {
  return pg_session_->IsInitDbDone();
}

Result<uint64_t> PgApiImpl::GetSharedCatalogVersion() {
  return pg_session_->GetSharedCatalogVersion();
}

// Transaction Control -----------------------------------------------------------------------------
Status PgApiImpl::BeginTransaction() {
  pg_session_->InvalidateForeignKeyReferenceCache();
  return pg_txn_manager_->BeginTransaction();
}

Status PgApiImpl::RestartTransaction() {
  pg_session_->InvalidateForeignKeyReferenceCache();
  return pg_txn_manager_->RestartTransaction();
}

Status PgApiImpl::CommitTransaction() {
  pg_session_->InvalidateForeignKeyReferenceCache();
  return pg_txn_manager_->CommitTransaction();
}

Status PgApiImpl::AbortTransaction() {
  pg_session_->InvalidateForeignKeyReferenceCache();
  return pg_txn_manager_->AbortTransaction();
}

Status PgApiImpl::SetTransactionIsolationLevel(int isolation) {
  return pg_txn_manager_->SetIsolationLevel(isolation);
}

Status PgApiImpl::SetTransactionReadOnly(bool read_only) {
  return pg_txn_manager_->SetReadOnly(read_only);
}

Status PgApiImpl::SetTransactionDeferrable(bool deferrable) {
  return pg_txn_manager_->SetDeferrable(deferrable);
}

Status PgApiImpl::EnterSeparateDdlTxnMode() {
  return pg_txn_manager_->EnterSeparateDdlTxnMode();
}

Status PgApiImpl::ExitSeparateDdlTxnMode(bool success) {
  return pg_txn_manager_->ExitSeparateDdlTxnMode(success);
}

bool PgApiImpl::ForeignKeyReferenceExists(YBCPgOid table_id, std::string&& ybctid) {
  return pg_session_->ForeignKeyReferenceExists(table_id, std::move(ybctid));
}

Status PgApiImpl::CacheForeignKeyReference(YBCPgOid table_id, std::string&& ybctid) {
  return pg_session_->CacheForeignKeyReference(table_id, std::move(ybctid));
}

Status PgApiImpl::DeleteForeignKeyReference(YBCPgOid table_id, std::string&& ybctid) {
  return pg_session_->DeleteForeignKeyReference(table_id, std::move(ybctid));
}

void PgApiImpl::ClearForeignKeyReferenceCache() {
  pg_session_->InvalidateForeignKeyReferenceCache();
}

} // namespace pggate
} // namespace yb
