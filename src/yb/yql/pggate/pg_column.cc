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

#include "yb/yql/pggate/pg_column.h"

namespace yb {
namespace pggate {

PgColumn::PgColumn() {
}

PgsqlExpressionPB *PgColumn::AllocPrimaryBindPB(PgsqlWriteRequestPB *write_req) {
  if (desc_.is_partition()) {
    bind_pb_ = write_req->add_partition_column_values();
  } else if (desc_.is_primary()) {
    bind_pb_ = write_req->add_range_column_values();
  }
  return bind_pb_;
}

PgsqlExpressionPB *PgColumn::AllocBindPB(PgsqlWriteRequestPB *write_req) {
  if (bind_pb_ == nullptr) {
    DCHECK(!desc_.is_partition() && !desc_.is_primary())
      << "Binds for primary columns should have already been allocated by AllocPrimaryBindPB()";

    PgsqlColumnValuePB* col_pb = write_req->add_column_values();
    col_pb->set_column_id(id());
    bind_pb_ = col_pb->mutable_expr();
  }
  return bind_pb_;
}

PgsqlExpressionPB *PgColumn::AllocPartitionBindPB(PgsqlReadRequestPB *read_req) {
  if (desc_.is_partition()) {
    bind_pb_ = read_req->add_partition_column_values();
  }
  return bind_pb_;
}

PgsqlExpressionPB *PgColumn::AllocBindPB(PgsqlReadRequestPB *read_req) {
  DCHECK(bind_pb_) << "Binds for partition columns should have already been allocated, "
                   << "and binding other columns are not allowed";
  return bind_pb_;
}

}  // namespace pggate
}  // namespace yb
