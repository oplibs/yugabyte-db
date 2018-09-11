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
//
// Structure definitions for a Postgres table descriptor.
//--------------------------------------------------------------------------------------------------

#ifndef YB_YQL_PGGATE_PG_TABLEDESC_H_
#define YB_YQL_PGGATE_PG_TABLEDESC_H_

#include "yb/client/client.h"
#include "yb/yql/pggate/pg_column.h"

namespace yb {
namespace pggate {

//--------------------------------------------------------------------------------------------------

// This class can be used to describe any reference of a column.
class PgTableDesc : public RefCountedThreadSafe<PgTableDesc> {
 public:

  //------------------------------------------------------------------------------------------------
  // Public types.
  typedef scoped_refptr<PgTableDesc> ScopedRefPtr;

  explicit PgTableDesc(std::shared_ptr<client::YBTable> pg_table);

  const client::YBTableName& table_name() const {
    return table_->name();
  }

  const std::shared_ptr<client::YBTable> table() const {
    return table_;
  }

  std::vector<PgColumn>& columns() {
    return columns_;
  }

  const int32_t num_hash_key_columns() const {
    return table_->schema().num_hash_key_columns();
  }

  const int32_t num_key_columns() const {
    return table_->schema().num_key_columns();
  }

  const int32_t num_columns() const {
    return table_->schema().num_columns();
  }

  client::YBPgsqlReadOp* NewPgsqlSelect() {
    return table_->NewPgsqlSelect();
  }

  client::YBPgsqlWriteOp* NewPgsqlInsert() {
    return table_->NewPgsqlInsert();
  }

  client::YBPgsqlWriteOp* NewPgsqlUpdate() {
    return table_->NewPgsqlUpdate();
  }

  client::YBPgsqlWriteOp* NewPgsqlDelete() {
    return table_->NewPgsqlDelete();
  }

  Result<PgColumn *> FindColumn(int attr_num) {
    for (auto& col : columns_) {
      if (col.attr_num() == attr_num) {
        return &col;
      }
    }

    return STATUS_SUBSTITUTE(InvalidArgument, "Invalid column number $0", attr_num);
  }

  CHECKED_STATUS GetColumnInfo(int16_t attr_number, bool *is_primary, bool *is_hash) const {
    for (int i = 0; i < num_key_columns(); i++) {
      if (columns_[i].attr_num() == attr_number) {
        *is_primary = true;
        *is_hash = i < num_hash_key_columns();
        return Status::OK();
      }
    }
    *is_primary = false;
    *is_hash = false;
    return Status::OK();
  }

 private:
  std::shared_ptr<client::YBTable> table_;

  // TODO(neil) Considering the posibility of indexing columns_ by attr_num instead of ID.
  std::vector<PgColumn> columns_;
};

}  // namespace pggate
}  // namespace yb

#endif  // YB_YQL_PGGATE_PG_TABLEDESC_H_
