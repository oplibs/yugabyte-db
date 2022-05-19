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

// Utilities for docdb operations.

#ifndef YB_DOCDB_DOCDB_UTIL_H
#define YB_DOCDB_DOCDB_UTIL_H

#include "yb/common/schema.h"

#include "yb/docdb/docdb_fwd.h"
#include "yb/docdb/doc_path.h"
#include "yb/docdb/doc_read_context.h"
#include "yb/docdb/shared_lock_manager_fwd.h"
#include "yb/docdb/doc_write_batch.h"
#include "yb/docdb/docdb_compaction_context.h"
#include "yb/rocksdb/compaction_filter.h"

namespace yb {
namespace docdb {

void SetValueFromQLBinaryWrapper(
  QLValuePB ql_value,
  const int pg_data_type,
  DatumMessagePB* cdc_datum_message = NULL);

struct ExternalIntent {
  DocPath doc_path;
  std::string value;
};

// A wrapper around a RocksDB instance and provides utility functions on top of it, such as
// compacting the history until a certain point. This is used in the bulk load tool. This is also
// convenient base class for GTest test classes, because it exposes member functions such as
// rocksdb() and write_options().
class DocDBRocksDBUtil {

 public:
  DocDBRocksDBUtil();
  explicit DocDBRocksDBUtil(InitMarkerBehavior init_marker_behavior);

  virtual ~DocDBRocksDBUtil() {}
  virtual Status InitRocksDBDir() = 0;

  // Initializes RocksDB options, should be called after constructor, because it uses virtual
  // function BlockCacheSize.
  virtual Status InitRocksDBOptions() = 0;
  virtual std::string tablet_id() = 0;

  // Size of block cache for RocksDB, 0 means don't use block cache.
  virtual size_t block_cache_size() const { return 16 * 1024 * 1024; }

  rocksdb::DB* rocksdb();
  rocksdb::DB* intents_db();
  DocDB doc_db() { return { rocksdb(), intents_db(), &KeyBounds::kNoBounds }; }

  Status InitCommonRocksDBOptionsForTests();
  Status InitCommonRocksDBOptionsForBulkLoad();

  const rocksdb::WriteOptions& write_options() const { return write_options_; }

  const rocksdb::Options& regular_db_options() const { return regular_db_options_; }

  Status OpenRocksDB();

  void CloseRocksDB();
  Status ReopenRocksDB();
  Status DestroyRocksDB();
  void ResetMonotonicCounter();

  // Populates the given RocksDB write batch from the given DocWriteBatch. If a valid hybrid_time is
  // specified, it is appended to every key.
  Status PopulateRocksDBWriteBatch(
      const DocWriteBatch& dwb,
      rocksdb::WriteBatch *rocksdb_write_batch,
      HybridTime hybrid_time = HybridTime::kInvalid,
      bool decode_dockey = true,
      bool increment_write_id = true,
      PartialRangeKeyIntents partial_range_key_intents = PartialRangeKeyIntents::kTrue) const;

  // Writes the given DocWriteBatch to RocksDB. We substitue the hybrid time, if provided.
  Status WriteToRocksDB(
      const DocWriteBatch& write_batch,
      const HybridTime& hybrid_time,
      bool decode_dockey = true,
      bool increment_write_id = true,
      PartialRangeKeyIntents partial_range_key_intents = PartialRangeKeyIntents::kTrue);

  // The same as WriteToRocksDB but also clears the write batch afterwards.
  Status WriteToRocksDBAndClear(DocWriteBatch* dwb, const HybridTime& hybrid_time,
                                        bool decode_dockey = true, bool increment_write_id = true);

  // Writes value fully determined by its index using DefaultWriteBatch.
  Status WriteSimple(int index);

  void SetHistoryCutoffHybridTime(HybridTime history_cutoff);

  // Produces a string listing the contents of the entire RocksDB database, with every key and value
  // decoded as a DocDB key/value and converted to a human-readable string representation.
  std::string DocDBDebugDumpToStr();

  // ----------------------------------------------------------------------------------------------
  // SetPrimitive taking a Value

  Status SetPrimitive(
      const DocPath& doc_path,
      const ValueRef& value,
      HybridTime hybrid_time,
      const ReadHybridTime& read_ht = ReadHybridTime::Max()) {
    return SetPrimitive(doc_path, ValueControlFields(), value, hybrid_time, read_ht);
  }

  Status SetPrimitive(
      const DocPath& doc_path,
      const ValueControlFields& control_fields,
      const ValueRef& value,
      HybridTime hybrid_time,
      const ReadHybridTime& read_ht = ReadHybridTime::Max());

  Status SetPrimitive(
      const DocPath& doc_path,
      const QLValuePB& value,
      HybridTime hybrid_time,
      const ReadHybridTime& read_ht = ReadHybridTime::Max());

  Status AddExternalIntents(
      const TransactionId& txn_id,
      const std::vector<ExternalIntent>& intents,
      const Uuid& involved_tablet,
      HybridTime hybrid_time);

  Status InsertSubDocument(
      const DocPath& doc_path,
      const ValueRef& value,
      HybridTime hybrid_time,
      MonoDelta ttl = ValueControlFields::kMaxTtl,
      const ReadHybridTime& read_ht = ReadHybridTime::Max());

  Status ExtendSubDocument(
      const DocPath& doc_path,
      const ValueRef& value,
      HybridTime hybrid_time,
      MonoDelta ttl = ValueControlFields::kMaxTtl,
      const ReadHybridTime& read_ht = ReadHybridTime::Max());

  Status ExtendList(
      const DocPath& doc_path,
      const ValueRef& value,
      HybridTime hybrid_time,
      const ReadHybridTime& read_ht = ReadHybridTime::Max());

  Status ReplaceInList(
      const DocPath &doc_path,
      const int target_cql_index,
      const ValueRef& value,
      const ReadHybridTime& read_ht,
      const HybridTime& hybrid_time,
      const rocksdb::QueryId query_id,
      MonoDelta default_ttl = ValueControlFields::kMaxTtl,
      MonoDelta ttl = ValueControlFields::kMaxTtl,
      UserTimeMicros user_timestamp = ValueControlFields::kInvalidUserTimestamp);

  Status DeleteSubDoc(
      const DocPath& doc_path,
      HybridTime hybrid_time,
      const ReadHybridTime& read_ht = ReadHybridTime::Max());

  void DocDBDebugDumpToConsole();

  Status FlushRocksDbAndWait();

  void SetTableTTL(uint64_t ttl_msec);

  void SetCurrentTransactionId(const TransactionId& txn_id) {
    current_txn_id_ = txn_id;
  }

  void SetTransactionIsolationLevel(IsolationLevel isolation_level) {
    txn_isolation_level_ = isolation_level;
  }

  void ResetCurrentTransactionId() {
    current_txn_id_.reset();
  }

  Status DisableCompactions() {
    regular_db_options_.compaction_style = rocksdb::kCompactionStyleNone;
    intents_db_options_.compaction_style = rocksdb::kCompactionStyleNone;
    return ReopenRocksDB();
  }

  Status ReinitDBOptions();

  std::atomic<int64_t>& monotonic_counter() {
    return monotonic_counter_;
  }

  DocWriteBatch MakeDocWriteBatch();
  DocWriteBatch MakeDocWriteBatch(InitMarkerBehavior init_marker_behavior);

  void SetInitMarkerBehavior(InitMarkerBehavior init_marker_behavior);

  // Returns DocWriteBatch created with MakeDocWriteBatch.
  // Could be used when single DocWriteBatch is enough.
  DocWriteBatch& DefaultDocWriteBatch();

 protected:
  std::string IntentsDBDir();

  std::unique_ptr<rocksdb::DB> regular_db_;
  std::unique_ptr<rocksdb::DB> intents_db_;
  rocksdb::Options regular_db_options_;
  rocksdb::Options intents_db_options_;
  std::string rocksdb_dir_;

  // This is used for auto-assigning op ids to RocksDB write batches to emulate what a tablet would
  // do in production.
  rocksdb::OpId op_id_;

  std::shared_ptr<rocksdb::Cache> block_cache_;
  std::shared_ptr<ManualHistoryRetentionPolicy> retention_policy_ {
      std::make_shared<ManualHistoryRetentionPolicy>() };
  std::shared_ptr<rocksdb::CompactionFileFilterFactory> compaction_file_filter_factory_;
  std::shared_ptr<std::function<uint64_t()>> max_file_size_for_compaction_;

  rocksdb::WriteOptions write_options_;
  DocReadContext doc_read_context_;
  boost::optional<TransactionId> current_txn_id_;
  mutable IntraTxnWriteId intra_txn_write_id_ = 0;
  IsolationLevel txn_isolation_level_ = IsolationLevel::NON_TRANSACTIONAL;
  InitMarkerBehavior init_marker_behavior_ = InitMarkerBehavior::kOptional;
  HybridTime delete_marker_retention_time_ = HybridTime::kMax;

 private:
  std::atomic<int64_t> monotonic_counter_{0};
  boost::optional<DocWriteBatch> doc_write_batch_;
};

}  // namespace docdb
}  // namespace yb

#endif // YB_DOCDB_DOCDB_UTIL_H
