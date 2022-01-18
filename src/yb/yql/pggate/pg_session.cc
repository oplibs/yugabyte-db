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

#include "yb/yql/pggate/pg_session.h"

#include <memory>

#include <boost/optional.hpp>

#include "yb/client/batcher.h"
#include "yb/client/error.h"
#include "yb/client/schema.h"
#include "yb/client/session.h"
#include "yb/client/table.h"
#include "yb/client/tablet_server.h"
#include "yb/client/transaction.h"
#include "yb/client/yb_op.h"
#include "yb/client/yb_table_name.h"

#include "yb/common/pg_types.h"
#include "yb/common/pgsql_error.h"
#include "yb/common/placement_info.h"
#include "yb/common/ql_expr.h"
#include "yb/common/ql_value.h"
#include "yb/common/row_mark.h"
#include "yb/common/schema.h"
#include "yb/common/transaction_error.h"

#include "yb/docdb/doc_key.h"
#include "yb/docdb/primitive_value.h"
#include "yb/docdb/value_type.h"

#include "yb/gutil/casts.h"

#include "yb/tserver/pg_client.pb.h"
#include "yb/tserver/tserver_shared_mem.h"

#include "yb/util/flag_tags.h"
#include "yb/util/format.h"
#include "yb/util/result.h"
#include "yb/util/shared_mem.h"
#include "yb/util/status_format.h"
#include "yb/util/string_util.h"

#include "yb/yql/pggate/pg_client.h"
#include "yb/yql/pggate/pg_expr.h"
#include "yb/yql/pggate/pg_txn_manager.h"
#include "yb/yql/pggate/pggate_flags.h"
#include "yb/yql/pggate/ybc_pggate.h"

using namespace std::literals;

DEFINE_int32(ysql_wait_until_index_permissions_timeout_ms, 60 * 60 * 1000, // 60 min.
             "DEPRECATED: use backfill_index_client_rpc_timeout_ms instead.");
TAG_FLAG(ysql_wait_until_index_permissions_timeout_ms, advanced);
DECLARE_int32(TEST_user_ddl_operation_timeout_sec);

DEFINE_bool(ysql_log_failed_docdb_requests, false, "Log failed docdb requests.");

namespace yb {
namespace pggate {

using std::make_shared;
using std::unique_ptr;
using std::shared_ptr;
using std::string;

using client::YBClient;
using client::YBSession;
using client::YBMetaDataCache;
using client::YBSchema;
using client::YBOperation;
using client::YBTable;
using client::YBTableName;
using client::YBTableType;

using yb::master::GetNamespaceInfoResponsePB;

using yb::tserver::TServerSharedObject;

namespace {

static constexpr const size_t kPgSequenceLastValueColIdx = 2;
static constexpr const size_t kPgSequenceIsCalledColIdx = 3;

string GetStatusStringSet(const client::CollectedErrors& errors) {
  std::set<string> status_strings;
  for (const auto& error : errors) {
    status_strings.insert(error->status().ToString());
  }
  return RangeToString(status_strings.begin(), status_strings.end());
}

bool IsHomogeneousErrors(const client::CollectedErrors& errors) {
  if (errors.size() < 2) {
    return true;
  }
  auto i = errors.begin();
  const auto& status = (**i).status();
  const auto codes = status.ErrorCodesSlice();
  for (++i; i != errors.end(); ++i) {
    const auto& s = (**i).status();
    if (s.code() != status.code() || codes != s.ErrorCodesSlice()) {
      return false;
    }
  }
  return true;
}

boost::optional<YBPgErrorCode> PsqlErrorCode(const Status& status) {
  const uint8_t* err_data = status.ErrorData(PgsqlErrorTag::kCategory);
  if (err_data) {
    return PgsqlErrorTag::Decode(err_data);
  }
  return boost::none;
}

// Get a common Postgres error code from the status and all errors, and append it to a previous
// Status.
// If any of those have different conflicting error codes, previous result is returned as-is.
CHECKED_STATUS AppendPsqlErrorCode(const Status& status,
                                   const client::CollectedErrors& errors) {
  boost::optional<YBPgErrorCode> common_psql_error =  boost::make_optional(false, YBPgErrorCode());
  for(const auto& error : errors) {
    const auto psql_error = PsqlErrorCode(error->status());
    if (!common_psql_error) {
      common_psql_error = psql_error;
    } else if (psql_error && common_psql_error != psql_error) {
      common_psql_error = boost::none;
      break;
    }
  }
  return common_psql_error ? status.CloneAndAddErrorCode(PgsqlError(*common_psql_error)) : status;
}

// Get a common transaction error code for all the errors and append it to the previous Status.
CHECKED_STATUS AppendTxnErrorCode(const Status& status, const client::CollectedErrors& errors) {
  TransactionErrorCode common_txn_error = TransactionErrorCode::kNone;
  for (const auto& error : errors) {
    const TransactionErrorCode txn_error = TransactionError(error->status()).value();
    if (txn_error == TransactionErrorCode::kNone ||
        txn_error == common_txn_error) {
      continue;
    }
    if (common_txn_error == TransactionErrorCode::kNone) {
      common_txn_error = txn_error;
      continue;
    }
    // If we receive a list of errors, with one as kConflict and others as kAborted, we retain the
    // error as kConflict, since in case of a batched request the first operation would receive the
    // kConflict and all the others would receive the kAborted error.
    if ((txn_error == TransactionErrorCode::kConflict &&
         common_txn_error == TransactionErrorCode::kAborted) ||
        (txn_error == TransactionErrorCode::kAborted &&
         common_txn_error == TransactionErrorCode::kConflict)) {
      common_txn_error = TransactionErrorCode::kConflict;
      continue;
    }

    // In all the other cases, reset the common_txn_error to kNone.
    common_txn_error = TransactionErrorCode::kNone;
    break;
  }

  return (common_txn_error != TransactionErrorCode::kNone) ?
    status.CloneAndAddErrorCode(TransactionError(common_txn_error)) : status;
}

// Given a set of errors from operations, this function attempts to combine them into one status
// that is later passed to PostgreSQL and further converted into a more specific error code.
CHECKED_STATUS CombineErrorsToStatus(const client::CollectedErrors& errors, const Status& status) {
  if (errors.empty())
    return status;

  if (status.IsIOError() &&
      // TODO: move away from string comparison here and use a more specific status than IOError.
      // See https://github.com/YugaByte/yugabyte-db/issues/702
      status.message() == client::internal::Batcher::kErrorReachingOutToTServersMsg &&
      IsHomogeneousErrors(errors)) {
    const auto& result = errors.front()->status();
    if (errors.size() == 1) {
      return result;
    }
    return Status(result.code(),
                  __FILE__,
                  __LINE__,
                  GetStatusStringSet(errors),
                  result.ErrorCodesSlice(),
                  DupFileName::kFalse);
  }

  Status result =
    status.ok()
    ? STATUS(InternalError, GetStatusStringSet(errors))
    : status.CloneAndAppend(". Errors from tablet servers: " + GetStatusStringSet(errors));

  return AppendTxnErrorCode(AppendPsqlErrorCode(result, errors), errors);
}

docdb::PrimitiveValue NullValue(SortingType sorting) {
  using SortingType = SortingType;

  return docdb::PrimitiveValue(
      sorting == SortingType::kAscendingNullsLast || sorting == SortingType::kDescendingNullsLast
          ? docdb::ValueType::kNullHigh
          : docdb::ValueType::kNullLow);
}

void InitKeyColumnPrimitiveValues(
    const google::protobuf::RepeatedPtrField<PgsqlExpressionPB> &column_values,
    const YBSchema &schema,
    size_t start_idx,
    vector<docdb::PrimitiveValue> *components) {
  size_t column_idx = start_idx;
  for (const auto& column_value : column_values) {
    const auto sorting_type = schema.Column(column_idx).sorting_type();
    if (column_value.has_value()) {
      const auto& value = column_value.value();
      components->push_back(
          IsNull(value)
          ? NullValue(sorting_type)
          : docdb::PrimitiveValue::FromQLValuePB(value, sorting_type));
    } else {
      // TODO(neil) The current setup only works for CQL as it assumes primary key value must not
      // be dependent on any column values. This needs to be fixed as PostgreSQL expression might
      // require a read from a table.
      //
      // Use regular executor for now.
      QLExprExecutor executor;
      QLExprResult result;
      auto s = executor.EvalExpr(column_value, nullptr, result.Writer());

      components->push_back(docdb::PrimitiveValue::FromQLValuePB(result.Value(), sorting_type));
    }
    ++column_idx;
  }
}

bool IsTableUsedByRequest(const PgsqlReadRequestPB& request, const string& table_id) {
  return request.table_id() == table_id ||
      (request.has_index_request() && IsTableUsedByRequest(request.index_request(), table_id));
}

bool IsTableUsedByRequest(const PgsqlWriteRequestPB& request, const string& table_id) {
  return request.table_id() == table_id;
}

bool IsTableUsedByOperation(const client::YBPgsqlOp& op, const string& table_id) {
  switch(op.type()) {
    case YBOperation::Type::PGSQL_READ:
      return IsTableUsedByRequest(
          down_cast<const client::YBPgsqlReadOp&>(op).request(), table_id);
    case YBOperation::Type::PGSQL_WRITE:
      return IsTableUsedByRequest(
          down_cast<const client::YBPgsqlWriteOp&>(op).request(), table_id);
    default:
      break;
  }
  DCHECK(false) << "Unexpected operation type " << op.type();
  return false;
}

struct PgForeignKeyReferenceLightweight {
  PgOid table_id;
  Slice ybctid;
};

size_t ForeignKeyReferenceHash(PgOid table_id, const char* begin, const char* end) {
  size_t hash = 0;
  boost::hash_combine(hash, table_id);
  boost::hash_range(hash, begin, end);
  return hash;
}

template<class Container>
auto Find(const Container& container, PgOid table_id, const Slice& ybctid) {
  return container.find(PgForeignKeyReferenceLightweight{table_id, ybctid},
      [](const auto& k) {
        return ForeignKeyReferenceHash(k.table_id, k.ybctid.cdata(), k.ybctid.cend()); },
      [](const auto& l, const auto& r) {
        return l.table_id == r.table_id && l.ybctid == r.ybctid; });
}

template<class Container>
bool Erase(Container* container, PgOid table_id, const Slice& ybctid) {
  const auto it = Find(*container, table_id, ybctid);
  if (it != container->end()) {
    container->erase(it);
    return true;
  }
  return false;
}

} // namespace

//--------------------------------------------------------------------------------------------------
// Class PgSessionAsyncRunResult
//--------------------------------------------------------------------------------------------------

PgSessionAsyncRunResult::PgSessionAsyncRunResult(PgsqlOpBuffer buffered_operations,
                                                 std::future<client::FlushStatus> future_status,
                                                 client::YBSessionPtr session)
    : buffered_operations_(std::move(buffered_operations)),
      future_status_(std::move(future_status)),
      session_(std::move(session)) {
}

Status PgSessionAsyncRunResult::GetStatus(PgSession* pg_session) {
  SCHECK(InProgress(), IllegalState, "Request must be in progress");
  const auto flush_status = future_status_.get();
  future_status_ = std::future<client::FlushStatus>();
  RETURN_NOT_OK(CombineErrorsToStatus(flush_status.errors, flush_status.status));
  for (const auto& bop : buffered_operations_) {
    RETURN_NOT_OK(pg_session->HandleResponse(*bop.operation, bop.relation_id));
  }
  return Status::OK();
}

bool PgSessionAsyncRunResult::InProgress() const {
  return future_status_.valid();
}

//--------------------------------------------------------------------------------------------------
// Class PgSession::RunHelper
//--------------------------------------------------------------------------------------------------

PgSession::RunHelper::RunHelper(const PgObjectId& relation_id,
                                PgSession* pg_session,
                                IsTransactionalSession transactional)
    : relation_id_(relation_id),
      pg_session_(*pg_session),
      transactional_(transactional),
      buffer_(transactional_ ? pg_session_.buffered_txn_ops_
                             : pg_session_.buffered_ops_) {
}

Status PgSession::RunHelper::Apply(std::shared_ptr<client::YBPgsqlOp> op,
                                   uint64_t* read_time,
                                   bool force_non_bufferable) {
  auto& buffered_keys = pg_session_.buffered_keys_;
  // Try buffering this operation if it is a write operation, buffering is enabled and no
  // operations have been already applied to current session (yb session does not exist).
  if (!yb_session_ &&
      pg_session_.buffering_enabled_ &&
      !force_non_bufferable &&
      op->type() == YBOperation::Type::PGSQL_WRITE) {
    const auto& wop = *down_cast<client::YBPgsqlWriteOp*>(op.get());
    // Check for buffered operation related to same row.
    // If multiple operations are performed in context of single RPC second operation will not
    // see the results of first operation on DocDB side.
    // Multiple operations on same row must be performed in context of different RPC.
    // Flush is required in this case.
    if (PREDICT_FALSE(!buffered_keys.insert(RowIdentifier(wop)).second)) {
      RETURN_NOT_OK(pg_session_.FlushBufferedOperations());
      buffered_keys.insert(RowIdentifier(wop));
    }
    if (PREDICT_FALSE(yb_debug_log_docdb_requests)) {
      LOG(INFO) << "Buffering operation: " << op->ToString();
    }
    buffer_.push_back({std::move(op), relation_id_});
    // Flush buffers in case limit of operations in single RPC exceeded.
    return PREDICT_TRUE(buffered_keys.size() < FLAGS_ysql_session_max_batch_size)
        ? Status::OK()
        : pg_session_.FlushBufferedOperations();
  }
  bool read_only = op->read_only();
  // Flush all buffered operations (if any) before performing non-bufferable operation
  if (!buffered_keys.empty()) {
    SCHECK(!yb_session_,
           IllegalState,
           "Buffered operations must be flushed before applying first non-bufferable operation");
    // Buffered operations can't be combined within single RPC with non bufferable operation
    // in case non bufferable operation has preset read_time.
    // Buffered operations must be flushed independently in this case.
    bool full_flush_required = (transactional_ && read_time && *read_time);
    // Check for buffered operation that affected same table as current operation.
    for (auto i = buffered_keys.begin(); !full_flush_required && i != buffered_keys.end(); ++i) {
      full_flush_required = IsTableUsedByOperation(*op, i->table_id());
    }
    if (full_flush_required) {
      RETURN_NOT_OK(pg_session_.FlushBufferedOperations());
    } else {
      RETURN_NOT_OK(pg_session_.FlushBufferedOperationsImpl(
          [this](auto ops, auto transactional) -> Status {
            if (transactional == transactional_) {
              // Save buffered operations for further applying before non-buffered operation.
              pending_ops_.swap(ops);
              return Status::OK();
            }
            return pg_session_.FlushOperations(std::move(ops), transactional);
          }));
      read_only = read_only && pending_ops_.empty();
    }
  }
  bool pessimistic_lock_required = false;
  if (op->type() == YBOperation::Type::PGSQL_READ) {
    const PgsqlReadRequestPB& read_req = down_cast<client::YBPgsqlReadOp*>(op.get())->request();
    auto row_mark_type = GetRowMarkTypeFromPB(read_req);
    read_only = read_only && !IsValidRowMarkType(row_mark_type);
    pessimistic_lock_required = RowMarkNeedsPessimisticLock(row_mark_type);
  }

  auto session = VERIFY_RESULT(pg_session_.GetSession(
      transactional_,
      IsReadOnlyOperation(read_only),
      IsPessimisticLockRequired(pessimistic_lock_required),
      IsCatalogOperation(op->IsYsqlCatalogOp())));
  if (!yb_session_) {
    yb_session_ = session->shared_from_this();
    if (transactional_ && read_time) {
      if (!*read_time) {
        *read_time = pg_session_.clock_->Now().ToUint64();
      }
      yb_session_->SetInTxnLimit(HybridTime(*read_time));
    }
    for (const auto& bop : pending_ops_) {
      RETURN_NOT_OK(pg_session_.ApplyOperation(yb_session_.get(), transactional_, bop));
    }
  } else {
    // Session must not be changed as all operations belong to single session
    // (transactional or non-transactional)
    DCHECK_EQ(yb_session_.get(), session);
  }
  if (PREDICT_FALSE(yb_debug_log_docdb_requests)) {
    LOG(INFO) << "Applying operation: " << op->ToString();
  }
  yb_session_->Apply(std::move(op));
  return Status::OK();
}

Result<PgSessionAsyncRunResult> PgSession::RunHelper::Flush() {
  if (yb_session_) {
    auto future_status = yb_session_->FlushFuture();
    return PgSessionAsyncRunResult(
        std::move(pending_ops_), std::move(future_status), std::move(yb_session_));
  }
  // All operations were buffered, no need to flush.
  return PgSessionAsyncRunResult();
}

//--------------------------------------------------------------------------------------------------
// Class PgForeignKeyReference
//--------------------------------------------------------------------------------------------------

PgForeignKeyReference::PgForeignKeyReference(PgOid tid, std::string yid) :
  table_id(tid), ybctid(std::move(yid)) {
}

bool operator==(const PgForeignKeyReference& k1, const PgForeignKeyReference& k2) {
  return k1.table_id == k2.table_id && k1.ybctid == k2.ybctid;
}

size_t hash_value(const PgForeignKeyReference& key) {
  return ForeignKeyReferenceHash(
      key.table_id, key.ybctid.c_str(), key.ybctid.c_str() + key.ybctid.length());
}

//--------------------------------------------------------------------------------------------------
// Class RowIdentifier
//--------------------------------------------------------------------------------------------------

RowIdentifier::RowIdentifier(const client::YBPgsqlWriteOp& op) :
  table_id_(&op.request().table_id()) {
  auto& request = op.request();
  if (request.has_ybctid_column_value()) {
    ybctid_ = &request.ybctid_column_value().value().binary_value();
  } else {
    vector<docdb::PrimitiveValue> hashed_components;
    vector<docdb::PrimitiveValue> range_components;
    const auto& schema = op.table()->schema();
    InitKeyColumnPrimitiveValues(request.partition_column_values(),
                                 schema,
                                 0 /* start_idx */,
                                 &hashed_components);
    InitKeyColumnPrimitiveValues(request.range_column_values(),
                                 schema,
                                 schema.num_hash_key_columns(),
                                 &range_components);
    if (hashed_components.empty()) {
      ybctid_holder_ = docdb::DocKey(std::move(range_components)).Encode().ToStringBuffer();
    } else {
      ybctid_holder_ = docdb::DocKey(request.hash_code(),
                                     std::move(hashed_components),
                                     std::move(range_components)).Encode().ToStringBuffer();
    }
    ybctid_ = nullptr;
  }
}

const string& RowIdentifier::ybctid() const {
  return ybctid_ ? *ybctid_ : ybctid_holder_;
}

const string& RowIdentifier::table_id() const {
  return *table_id_;
}

bool operator==(const RowIdentifier& k1, const RowIdentifier& k2) {
  return k1.table_id() == k2.table_id() && k1.ybctid() == k2.ybctid();
}

size_t hash_value(const RowIdentifier& key) {
  size_t hash = 0;
  boost::hash_combine(hash, key.table_id());
  boost::hash_combine(hash, key.ybctid());
  return hash;
}

//--------------------------------------------------------------------------------------------------
// Class PgSession
//--------------------------------------------------------------------------------------------------

PgSession::PgSession(
    client::YBClient* client,
    PgClient* pg_client,
    const string& database_name,
    scoped_refptr<PgTxnManager> pg_txn_manager,
    scoped_refptr<server::HybridClock> clock,
    const tserver::TServerSharedObject* tserver_shared_object,
    const YBCPgCallbacks& pg_callbacks)
    : client_(client),
      session_(BuildSession(client_)),
      pg_client_(*pg_client),
      pg_txn_manager_(std::move(pg_txn_manager)),
      clock_(std::move(clock)),
      catalog_session_(BuildSession(client_, clock_)),
      tserver_shared_object_(tserver_shared_object),
      pg_callbacks_(pg_callbacks) {
}

PgSession::~PgSession() {
}

//--------------------------------------------------------------------------------------------------

Status PgSession::ConnectDatabase(const string& database_name) {
  connected_database_ = database_name;
  return Status::OK();
}

Status PgSession::IsDatabaseColocated(const PgOid database_oid, bool *colocated) {
  auto resp = VERIFY_RESULT(pg_client_.GetDatabaseInfo(database_oid));
  *colocated = resp.colocated();
  return Status::OK();
}

//--------------------------------------------------------------------------------------------------

Status PgSession::CreateDatabase(const string& database_name,
                                 const PgOid database_oid,
                                 const PgOid source_database_oid,
                                 const PgOid next_oid,
                                 const boost::optional<TransactionMetadata> transaction,
                                 const bool colocated) {
  tserver::PgCreateDatabaseRequestPB req;
  req.set_database_name(database_name);
  req.set_database_oid(database_oid);
  req.set_source_database_oid(source_database_oid);
  req.set_next_oid(next_oid);
  if (transaction) {
    transaction->ToPB(req.mutable_use_transaction());
  }
  req.set_colocated(colocated);
  CoarseTimePoint deadline;
  if (PREDICT_FALSE(FLAGS_TEST_user_ddl_operation_timeout_sec > 0)) {
    deadline = CoarseMonoClock::now() + FLAGS_TEST_user_ddl_operation_timeout_sec * 1s;
  }
  return pg_client_.CreateDatabase(&req, deadline);
}

Status PgSession::DropDatabase(const string& database_name, PgOid database_oid) {
  tserver::PgDropDatabaseRequestPB req;
  req.set_database_name(database_name);
  req.set_database_oid(database_oid);

  RETURN_NOT_OK(pg_client_.DropDatabase(&req, CoarseTimePoint()));
  RETURN_NOT_OK(DeleteDBSequences(database_oid));
  return Status::OK();
}

Status PgSession::GetCatalogMasterVersion(uint64_t *version) {
  *version = VERIFY_RESULT(pg_client_.GetCatalogMasterVersion());
  return Status::OK();
}

Status PgSession::CreateSequencesDataTable() {
  return pg_client_.CreateSequencesDataTable();
}

Status PgSession::InsertSequenceTuple(int64_t db_oid,
                                      int64_t seq_oid,
                                      uint64_t ysql_catalog_version,
                                      int64_t last_val,
                                      bool is_called) {
  PgObjectId oid(kPgSequencesDataDatabaseOid, kPgSequencesDataTableOid);
  auto result = LoadTable(oid);
  if (!result.ok()) {
    RETURN_NOT_OK(CreateSequencesDataTable());
    // Try one more time.
    result = LoadTable(oid);
  }
  auto t = VERIFY_RESULT(std::move(result));

  auto psql_write(t->NewPgsqlInsert());

  auto write_request = psql_write->mutable_request();
  write_request->set_ysql_catalog_version(ysql_catalog_version);

  write_request->add_partition_column_values()->mutable_value()->set_int64_value(db_oid);
  write_request->add_partition_column_values()->mutable_value()->set_int64_value(seq_oid);

  PgsqlColumnValuePB* column_value = write_request->add_column_values();
  column_value->set_column_id(t->schema().column_id(kPgSequenceLastValueColIdx));
  column_value->mutable_expr()->mutable_value()->set_int64_value(last_val);

  column_value = write_request->add_column_values();
  column_value->set_column_id(t->schema().column_id(kPgSequenceIsCalledColIdx));
  column_value->mutable_expr()->mutable_value()->set_bool_value(is_called);

  return session_->ApplyAndFlush(std::move(psql_write));
}

Status PgSession::UpdateSequenceTuple(int64_t db_oid,
                                      int64_t seq_oid,
                                      uint64_t ysql_catalog_version,
                                      int64_t last_val,
                                      bool is_called,
                                      boost::optional<int64_t> expected_last_val,
                                      boost::optional<bool> expected_is_called,
                                      bool* skipped) {
  PgObjectId oid(kPgSequencesDataDatabaseOid, kPgSequencesDataTableOid);
  auto t = VERIFY_RESULT(LoadTable(oid));

  std::shared_ptr<client::YBPgsqlWriteOp> psql_write(t->NewPgsqlUpdate());

  auto write_request = psql_write->mutable_request();
  write_request->set_ysql_catalog_version(ysql_catalog_version);

  write_request->add_partition_column_values()->mutable_value()->set_int64_value(db_oid);
  write_request->add_partition_column_values()->mutable_value()->set_int64_value(seq_oid);

  PgsqlColumnValuePB* column_value = write_request->add_column_new_values();
  column_value->set_column_id(t->schema().column_id(kPgSequenceLastValueColIdx));
  column_value->mutable_expr()->mutable_value()->set_int64_value(last_val);

  column_value = write_request->add_column_new_values();
  column_value->set_column_id(t->schema().column_id(kPgSequenceIsCalledColIdx));
  column_value->mutable_expr()->mutable_value()->set_bool_value(is_called);

  auto where_pb = write_request->mutable_where_expr()->mutable_condition();

  if (expected_last_val && expected_is_called) {
    // WHERE clause => WHERE last_val == expected_last_val AND is_called == expected_is_called.
    where_pb->set_op(QL_OP_AND);

    auto cond = where_pb->add_operands()->mutable_condition();
    cond->set_op(QL_OP_EQUAL);
    cond->add_operands()->set_column_id(t->schema().column_id(kPgSequenceLastValueColIdx));
    cond->add_operands()->mutable_value()->set_int64_value(*expected_last_val);

    cond = where_pb->add_operands()->mutable_condition();
    cond->set_op(QL_OP_EQUAL);
    cond->add_operands()->set_column_id(t->schema().column_id(kPgSequenceIsCalledColIdx));
    cond->add_operands()->mutable_value()->set_bool_value(*expected_is_called);
  } else {
    where_pb->set_op(QL_OP_EXISTS);
  }

  write_request->mutable_column_refs()->add_ids(
      t->schema().column_id(kPgSequenceLastValueColIdx));
  write_request->mutable_column_refs()->add_ids(
      t->schema().column_id(kPgSequenceIsCalledColIdx));

  RETURN_NOT_OK(session_->ApplyAndFlush(psql_write));
  if (skipped) {
    *skipped = psql_write->response().skipped();
  }
  return Status::OK();
}

Status PgSession::ReadSequenceTuple(int64_t db_oid,
                                    int64_t seq_oid,
                                    uint64_t ysql_catalog_version,
                                    int64_t *last_val,
                                    bool *is_called) {
  PgObjectId oid(kPgSequencesDataDatabaseOid, kPgSequencesDataTableOid);
  PgTableDescPtr t = VERIFY_RESULT(LoadTable(oid));

  std::shared_ptr<client::YBPgsqlReadOp> psql_read(t->NewPgsqlSelect());

  auto read_request = psql_read->mutable_request();
  read_request->set_ysql_catalog_version(ysql_catalog_version);

  read_request->add_partition_column_values()->mutable_value()->set_int64_value(db_oid);
  read_request->add_partition_column_values()->mutable_value()->set_int64_value(seq_oid);

  read_request->add_targets()->set_column_id(
      t->schema().column_id(kPgSequenceLastValueColIdx));
  read_request->add_targets()->set_column_id(
      t->schema().column_id(kPgSequenceIsCalledColIdx));

  read_request->mutable_column_refs()->add_ids(
      t->schema().column_id(kPgSequenceLastValueColIdx));
  read_request->mutable_column_refs()->add_ids(
      t->schema().column_id(kPgSequenceIsCalledColIdx));

  RETURN_NOT_OK(session_->ReadSync(psql_read));

  Slice cursor;
  int64_t row_count = 0;
  PgDocData::LoadCache(psql_read->rows_data(), &row_count, &cursor);
  if (row_count == 0) {
    return STATUS_SUBSTITUTE(NotFound, "Unable to find relation for sequence $0", seq_oid);
  }

  PgWireDataHeader header = PgDocData::ReadDataHeader(&cursor);
  if (header.is_null()) {
    return STATUS_SUBSTITUTE(NotFound, "Unable to find relation for sequence $0", seq_oid);
  }
  size_t read_size = PgDocData::ReadNumber(&cursor, last_val);
  cursor.remove_prefix(read_size);

  header = PgDocData::ReadDataHeader(&cursor);
  if (header.is_null()) {
    return STATUS_SUBSTITUTE(NotFound, "Unable to find relation for sequence $0", seq_oid);
  }
  read_size = PgDocData::ReadNumber(&cursor, is_called);
  return Status::OK();
}

Status PgSession::DeleteSequenceTuple(int64_t db_oid, int64_t seq_oid) {
  PgObjectId oid(kPgSequencesDataDatabaseOid, kPgSequencesDataTableOid);
  PgTableDescPtr t = VERIFY_RESULT(LoadTable(oid));

  auto psql_delete(t->NewPgsqlDelete());
  auto delete_request = psql_delete->mutable_request();

  delete_request->add_partition_column_values()->mutable_value()->set_int64_value(db_oid);
  delete_request->add_partition_column_values()->mutable_value()->set_int64_value(seq_oid);

  return session_->ApplyAndFlush(std::move(psql_delete));
}

Status PgSession::DeleteDBSequences(int64_t db_oid) {
  PgObjectId oid(kPgSequencesDataDatabaseOid, kPgSequencesDataTableOid);
  Result<PgTableDescPtr> r = LoadTable(oid);
  if (!r.ok()) {
    // Sequence table is not yet created.
    return Status::OK();
  }

  auto t = std::move(*r);
  if (t == nullptr) {
    return Status::OK();
  }

  auto psql_delete(t->NewPgsqlDelete());
  auto delete_request = psql_delete->mutable_request();

  delete_request->add_partition_column_values()->mutable_value()->set_int64_value(db_oid);
  return session_->ApplyAndFlush(std::move(psql_delete));
}

//--------------------------------------------------------------------------------------------------

Status PgSession::DropTable(const PgObjectId& table_id) {
  tserver::PgDropTableRequestPB req;
  table_id.ToPB(req.mutable_table_id());
  return ResultToStatus(pg_client_.DropTable(&req, CoarseTimePoint()));
}

Status PgSession::DropIndex(
    const PgObjectId& index_id,
    client::YBTableName* indexed_table_name) {
  tserver::PgDropTableRequestPB req;
  index_id.ToPB(req.mutable_table_id());
  req.set_index(true);
  auto result = VERIFY_RESULT(pg_client_.DropTable(&req, CoarseTimePoint()));
  if (indexed_table_name) {
    *indexed_table_name = std::move(result);
  }
  return Status::OK();
}

Status PgSession::DropTablegroup(const PgOid database_oid,
                                 PgOid tablegroup_oid) {
  tserver::PgDropTablegroupRequestPB req;
  PgObjectId tablegroup_id(database_oid, tablegroup_oid);
  tablegroup_id.ToPB(req.mutable_tablegroup_id());
  Status s = pg_client_.DropTablegroup(&req, CoarseTimePoint());
  table_cache_.erase(PgObjectId(database_oid, tablegroup_oid));
  return s;
}

//--------------------------------------------------------------------------------------------------

Result<PgTableDescPtr> PgSession::LoadTable(const PgObjectId& table_id) {
  VLOG(3) << "Loading table descriptor for " << table_id;

  auto cached_table_it = table_cache_.find(table_id);
  if (cached_table_it != table_cache_.end()) {
    return cached_table_it->second;
  }

  VLOG(4) << "Table cache MISS: " << table_id;
  auto table = VERIFY_RESULT(pg_client_.OpenTable(table_id));
  table_cache_.emplace(table_id, table);
  return table;
}

void PgSession::InvalidateTableCache(const PgObjectId& table_id) {
  table_cache_.erase(table_id);
}

Status PgSession::StartOperationsBuffering() {
  SCHECK(!buffering_enabled_, IllegalState, "Buffering has been already started");
  if (PREDICT_FALSE(!buffered_keys_.empty())) {
    LOG(DFATAL) << "Buffering hasn't been started yet but "
                << buffered_keys_.size()
                << " buffered operations found";
  }
  buffering_enabled_ = true;
  return Status::OK();
}

Status PgSession::StopOperationsBuffering() {
  SCHECK(buffering_enabled_, IllegalState, "Buffering hasn't been started");
  buffering_enabled_ = false;
  return FlushBufferedOperations();
}

void PgSession::ResetOperationsBuffering() {
  DropBufferedOperations();
  buffering_enabled_ = false;
}

Status PgSession::FlushBufferedOperations() {
  return FlushBufferedOperationsImpl(
      [this](auto ops, auto txn) { return this->FlushOperations(std::move(ops), txn); });
}

void PgSession::DropBufferedOperations() {
  VLOG_IF(1, !buffered_keys_.empty())
          << "Dropping " << buffered_keys_.size() << " pending operations";
  buffered_keys_.clear();
  buffered_ops_.clear();
  buffered_txn_ops_.clear();
}

Status PgSession::FlushBufferedOperationsImpl(const Flusher& flusher) {
  auto ops = std::move(buffered_ops_);
  auto txn_ops = std::move(buffered_txn_ops_);
  buffered_keys_.clear();
  buffered_ops_.clear();
  buffered_txn_ops_.clear();
  if (!ops.empty()) {
    RETURN_NOT_OK(flusher(std::move(ops), IsTransactionalSession::kFalse));
  }
  if (!txn_ops.empty()) {
    SCHECK(!YBCIsInitDbModeEnvVarSet(),
           IllegalState,
           "No transactional operations are expected in the initdb mode");
    RETURN_NOT_OK(flusher(std::move(txn_ops), IsTransactionalSession::kTrue));
  }
  return Status::OK();
}

Result<bool> PgSession::ShouldHandleTransactionally(const client::YBPgsqlOp& op) {
  if (!op.IsTransactional() || YBCIsInitDbModeEnvVarSet()) {
    return false;
  }
  const auto has_non_ddl_txn = pg_txn_manager_->IsTxnInProgress();
  if (!op.IsYsqlCatalogOp()) {
    SCHECK(has_non_ddl_txn, IllegalState, "Transactional operation requires transaction");
    return true;
  }
  if (pg_txn_manager_->IsDdlMode() || (yb_non_ddl_txn_for_sys_tables_allowed && has_non_ddl_txn)) {
    return true;
  }
  if (op.type() == YBOperation::Type::PGSQL_WRITE) {
    // For consistent read from catalog tables all write operations must be done in transaction.
    return STATUS_FORMAT(IllegalState,
                         "Transaction for catalog table write operation '$0' not found",
                         op.table()->name().table_name());
  }
  return false;
}

Result<YBSession*> PgSession::GetSession(IsTransactionalSession transactional,
                                         IsReadOnlyOperation read_only_op,
                                         IsPessimisticLockRequired pessimistic_lock_required,
                                         IsCatalogOperation is_catalog_op) {
  if (transactional) {
    YBSession* txn_session = VERIFY_RESULT(pg_txn_manager_->GetTransactionalSession());
    RETURN_NOT_OK(pg_txn_manager_->BeginWriteTransactionIfNecessary(read_only_op,
                                                                    pessimistic_lock_required));
    VLOG(2) << __PRETTY_FUNCTION__
            << ": read_only_op=" << read_only_op << ", returning transactional session: "
            << txn_session;
    return txn_session;
  }
  YBSession* non_txn_session = is_catalog_op && read_only_op && !YBCIsInitDbModeEnvVarSet()
      ? catalog_session_.get() : session_.get();
  VLOG(2) << __PRETTY_FUNCTION__
          << ": read_only_op=" << read_only_op << ", returning non-transactional session "
          << non_txn_session;
  return non_txn_session;
}

Result<bool> PgSession::IsInitDbDone() {
  return pg_client_.IsInitDbDone();
}

Status PgSession::ApplyOperation(client::YBSession *session,
                                 bool transactional,
                                 const BufferableOperation& bop) {
  const auto& op = bop.operation;
  SCHECK_EQ(VERIFY_RESULT(ShouldHandleTransactionally(*op)),
            transactional,
            IllegalState,
            Format("Table name: $0, table is transactional: $1, initdb mode: $2",
                   op->table()->name(),
                   op->table()->schema().table_properties().is_transactional(),
                   YBCIsInitDbModeEnvVarSet()));
  session->Apply(op);
  return Status::OK();
}

Status PgSession::FlushOperations(PgsqlOpBuffer ops, IsTransactionalSession transactional) {
  DCHECK(ops.size() > 0 && ops.size() <= FLAGS_ysql_session_max_batch_size);
  auto session = VERIFY_RESULT(GetSession(transactional, IsReadOnlyOperation::kFalse));
  if (session != session_.get()) {
    DCHECK(transactional);
    session->SetInTxnLimit(HybridTime(clock_->Now().ToUint64()));
  }
  if (PREDICT_FALSE(yb_debug_log_docdb_requests)) {
    LOG(INFO) << "Flushing buffered operations, using "
              << (transactional ? "transactional" : "non-transactional")
              << " session (num ops: " << ops.size() << ")";
  }
  for (const auto& buffered_op : ops) {
    RETURN_NOT_OK(ApplyOperation(session, transactional, buffered_op));
  }
  const auto flush_status = session->FlushFuture().get();
  RETURN_NOT_OK(CombineErrorsToStatus(flush_status.errors, flush_status.status));
  for (const auto& buffered_op : ops) {
    RETURN_NOT_OK(HandleResponse(*buffered_op.operation, buffered_op.relation_id));
  }
  return Status::OK();
}

Result<uint64_t> PgSession::GetSharedCatalogVersion() {
  if (tserver_shared_object_) {
    return (**tserver_shared_object_).ysql_catalog_version();
  } else {
    return STATUS(NotSupported, "Tablet server shared memory has not been opened");
  }
}

Result<uint64_t> PgSession::GetSharedAuthKey() {
  if (tserver_shared_object_) {
    return (**tserver_shared_object_).postgres_auth_key();
  } else {
    return STATUS(NotSupported, "Tablet server shared memory has not been opened");
  }
}

Result<bool> PgSession::ForeignKeyReferenceExists(PgOid table_id,
                                                  const Slice& ybctid,
                                                  const YbctidReader& reader) {
  if (Find(fk_reference_cache_, table_id, ybctid) != fk_reference_cache_.end()) {
    return true;
  }

  // Check existence of required FK intent.
  // Absence means the key was checked by previous batched request and was not found.
  if (!Erase(&fk_reference_intent_, table_id, ybctid)) {
    return false;
  }
  std::vector<Slice> ybctids;
  const auto reserved_size = std::min<size_t>(FLAGS_ysql_session_max_batch_size,
                                              fk_reference_intent_.size() + 1);
  ybctids.reserve(reserved_size);
  ybctids.push_back(ybctid);
  // TODO(dmitry): In case number of keys for same table > FLAGS_ysql_session_max_batch_size
  // two strategy are possible:
  // 1. select keys belonging to same tablet to reduce number of simultaneous RPC
  // 2. select keys belonging to different tablets to distribute reads among different nodes
  const auto intent_match = [table_id](const auto& key) { return key.table_id == table_id; };
  for (auto it = fk_reference_intent_.begin();
       it != fk_reference_intent_.end() && ybctids.size() < FLAGS_ysql_session_max_batch_size;
       ++it) {
    if (intent_match(*it)) {
      ybctids.push_back(it->ybctid);
    }
  }
  for (auto& r : VERIFY_RESULT(reader(table_id, ybctids))) {
    fk_reference_cache_.emplace(table_id, std::move(r));
  }
  // Remove used intents.
  auto intent_count_for_remove = ybctids.size() - 1;
  if (intent_count_for_remove == fk_reference_intent_.size()) {
    fk_reference_intent_.clear();
  } else {
    for (auto it = fk_reference_intent_.begin();
        it != fk_reference_intent_.end() && intent_count_for_remove > 0;) {
      if (intent_match(*it)) {
        it = fk_reference_intent_.erase(it);
        --intent_count_for_remove;
      } else {
        ++it;
      }
    }
  }
  return Find(fk_reference_cache_, table_id, ybctid) != fk_reference_cache_.end();
}

void PgSession::AddForeignKeyReferenceIntent(PgOid table_id, const Slice& ybctid) {
  if (Find(fk_reference_cache_, table_id, ybctid) == fk_reference_cache_.end()) {
    fk_reference_intent_.emplace(table_id, ybctid.ToBuffer());
  }
}

void PgSession::AddForeignKeyReference(PgOid table_id, const Slice& ybctid) {
  if (Find(fk_reference_cache_, table_id, ybctid) == fk_reference_cache_.end()) {
    fk_reference_cache_.emplace(table_id, ybctid.ToBuffer());
  }
}

void PgSession::DeleteForeignKeyReference(PgOid table_id, const Slice& ybctid) {
  Erase(&fk_reference_cache_, table_id, ybctid);
}

Status PgSession::HandleResponse(const client::YBPgsqlOp& op, const PgObjectId& relation_id) {
  if (op.succeeded()) {
    if (op.type() == YBOperation::PGSQL_READ && op.IsYsqlCatalogOp()) {
      const auto& pgsql_op = down_cast<const client::YBPgsqlReadOp&>(op);
      if (pgsql_op.used_read_time()) {
        // Non empty used_read_time field in catalog read operation means this is the very first
        // catalog read operation after catalog read time resetting. read_time for the operation
        // has been chosen by master. All further reads from catalog must use same read point.
        auto catalog_read_point = pgsql_op.used_read_time();

        // We set global limit to local limit to avoid read restart errors because they are
        // disruptive to system catalog reads and it is not always possible to handle them there.
        // This might lead to reading slightly outdated state of the system catalog if a recently
        // committed DDL transaction used a transaction status tablet whose leader's clock is skewed
        // and is in the future compared to the master leader's clock.
        // TODO(dmitry) This situation will be handled in context of #7964.
        catalog_read_point.global_limit = catalog_read_point.local_limit;
        SetCatalogReadPoint(catalog_read_point);
      }
    }
    return Status::OK();
  }
  const auto& response = op.response();
  YBPgErrorCode pg_error_code = YBPgErrorCode::YB_PG_INTERNAL_ERROR;
  if (response.has_pg_error_code()) {
    pg_error_code = static_cast<YBPgErrorCode>(response.pg_error_code());
  }

  TransactionErrorCode txn_error_code = TransactionErrorCode::kNone;
  if (response.has_txn_error_code()) {
    txn_error_code = static_cast<TransactionErrorCode>(response.txn_error_code());
  }

  Status s;
  if (response.status() == PgsqlResponsePB::PGSQL_STATUS_DUPLICATE_KEY_ERROR) {
    char constraint_name[0xFF];
    constraint_name[sizeof(constraint_name) - 1] = 0;
    pg_callbacks_.FetchUniqueConstraintName(relation_id.object_oid,
                                            constraint_name,
                                            sizeof(constraint_name) - 1);
    s = STATUS(
        AlreadyPresent,
        Format("duplicate key value violates unique constraint \"$0\"", Slice(constraint_name)),
        Slice(),
        PgsqlError(YBPgErrorCode::YB_PG_UNIQUE_VIOLATION));
  } else {
    if (PREDICT_FALSE(yb_debug_log_docdb_requests || FLAGS_ysql_log_failed_docdb_requests)) {
      LOG(INFO) << "Operation failed: " << op.ToString();
    }
    s = STATUS(QLError, op.response().error_message(), Slice(),
               PgsqlError(pg_error_code));
  }
  return s.CloneAndAddErrorCode(TransactionError(txn_error_code));
}

Result<int> PgSession::TabletServerCount(bool primary_only) {
  return pg_client_.TabletServerCount(primary_only);
}

Result<client::TabletServersInfo> PgSession::ListTabletServers() {
  return pg_client_.ListLiveTabletServers(false);
}

bool PgSession::ShouldUseFollowerReads() const {
  return pg_txn_manager_->ShouldUseFollowerReads();
}

void PgSession::SetTimeout(const int timeout_ms) {
  session_->SetTimeout(MonoDelta::FromMilliseconds(timeout_ms));
}

void PgSession::ResetCatalogReadPoint() {
  catalog_session_->SetReadPoint(ReadHybridTime());
}

void PgSession::SetCatalogReadPoint(const ReadHybridTime& read_ht) {
  catalog_session_->SetReadPoint(read_ht);
}

Status PgSession::ValidatePlacement(const string& placement_info) {
  tserver::PgValidatePlacementRequestPB req;

  Result<PlacementInfoConverter::Placement> result =
      PlacementInfoConverter::FromString(placement_info);

  // For validation, if there is no replica_placement option, we default to the
  // cluster configuration which the user is responsible for maintaining
  if (!result.ok() && result.status().IsInvalidArgument()) {
    return Status::OK();
  }

  RETURN_NOT_OK(result);

  PlacementInfoConverter::Placement placement = result.get();
  for (const auto& block : placement.placement_infos) {
    auto pb = req.add_placement_infos();
    pb->set_cloud(block.cloud);
    pb->set_region(block.region);
    pb->set_zone(block.zone);
    pb->set_min_num_replicas(block.min_num_replicas);
  }
  req.set_num_replicas(placement.num_replicas);

  return pg_client_.ValidatePlacement(&req);
}

}  // namespace pggate
}  // namespace yb
