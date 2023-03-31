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

#pragma once

#include <atomic>
#include <string>
#include <variant>

#include "yb/docdb/doc_rowwise_iterator_base.h"
#include "yb/docdb/doc_reader.h"
#include "yb/rocksdb/db.h"

#include "yb/common/hybrid_time.h"
#include "yb/common/ql_scanspec.h"
#include "yb/common/read_hybrid_time.h"
#include "yb/common/schema.h"

#include "yb/docdb/doc_pgsql_scanspec.h"
#include "yb/docdb/doc_ql_scanspec.h"
#include "yb/docdb/key_bounds.h"
#include "yb/docdb/ql_rowwise_iterator_interface.h"
#include "yb/docdb/subdocument.h"
#include "yb/docdb/value.h"

#include "yb/util/status_fwd.h"
#include "yb/util/operation_counter.h"
#include "yb/docdb/doc_read_context.h"

namespace yb {
namespace docdb {

class IntentAwareIterator;
class ScanChoices;
struct FetchKeyResult;

// An SQL-mapped-to-document-DB iterator.
class DocRowwiseIterator : public DocRowwiseIteratorBase {
 public:
  DocRowwiseIterator(const Schema &projection,
                     std::reference_wrapper<const DocReadContext> doc_read_context,
                     const TransactionOperationContext& txn_op_context,
                     const DocDB& doc_db,
                     CoarseTimePoint deadline,
                     const ReadHybridTime& read_time,
                     RWOperationCounter* pending_op_counter = nullptr,
                     boost::optional<size_t> end_referenced_key_column_index = boost::none);

  DocRowwiseIterator(std::unique_ptr<Schema> projection,
                     std::shared_ptr<DocReadContext> doc_read_context,
                     const TransactionOperationContext& txn_op_context,
                     const DocDB& doc_db,
                     CoarseTimePoint deadline,
                     const ReadHybridTime& read_time,
                     RWOperationCounter* pending_op_counter = nullptr,
                     boost::optional<size_t> end_referenced_key_column_index = boost::none);

  DocRowwiseIterator(std::unique_ptr<Schema> projection,
                     std::reference_wrapper<const DocReadContext> doc_read_context,
                     const TransactionOperationContext& txn_op_context,
                     const DocDB& doc_db,
                     CoarseTimePoint deadline,
                     const ReadHybridTime& read_time,
                     RWOperationCounter* pending_op_counter = nullptr,
                     boost::optional<size_t> end_referenced_key_column_index = boost::none);

  ~DocRowwiseIterator() override;

  // This must always be called before NextRow. The implementation actually finds the
  // first row to scan, and NextRow expects the RocksDB iterator to already be properly
  // positioned.
  Result<bool> HasNext() override;

  std::string ToString() const override;

  // Check if liveness column exists. Should be called only after HasNext() has been called to
  // verify the row exists.
  bool LivenessColumnExists() const;

  Result<HybridTime> RestartReadHt() override;

  HybridTime TEST_MaxSeenHt() override;

 private:
  void InitIterator(
      BloomFilterMode bloom_filter_mode = BloomFilterMode::DONT_USE_BLOOM_FILTER,
      const boost::optional<const Slice>& user_key_for_filter = boost::none,
      const rocksdb::QueryId query_id = rocksdb::kDefaultQueryId,
      std::shared_ptr<rocksdb::ReadFileFilter> file_filter = nullptr) override;

  void Seek(const Slice& key) override;
  void PrevDocKey(const Slice& key) override;

 private:
  void ConfigureForYsql();
  void InitResult();

  // For reverse scans, moves the iterator to the first kv-pair of the previous row after having
  // constructed the current row. For forward scans nothing is necessary because GetSubDocument
  // ensures that the iterator will be positioned on the first kv-pair of the next row.
  Status AdvanceIteratorToNextDesiredRow() const;

  // Read next row into a value map using the specified projection.
  Status DoNextRow(boost::optional<const Schema&> projection, QLTableRow* table_row) override;

  std::unique_ptr<IntentAwareIterator> db_iter_;

  IsFlatDoc is_flat_doc_ = IsFlatDoc::kFalse;

  // HasNext constructs the whole row's SubDocument or vector of values.
  std::variant<std::monostate, SubDocument, std::vector<QLValuePB>> result_;
  // Points to appropriate alternative owned by result_ field.
  SubDocument* row_;
  std::vector<QLValuePB>* values_;

  std::unique_ptr<DocDBTableReader> doc_reader_;
};

}  // namespace docdb
}  // namespace yb
