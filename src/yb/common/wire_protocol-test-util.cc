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

#include "yb/common/wire_protocol-test-util.h"

#include "yb/docdb/doc_key.h"

namespace yb {

void AddKVToPB(int32_t key_val,
               int32_t int_val,
               const string& string_val,
               docdb::KeyValueWriteBatchPB* write_batch) {
  const ColumnId int_val_col_id(kFirstColumnId + 1);
  const ColumnId string_val_col_id(kFirstColumnId + 2);

  auto add_kv_pair =
    [&](const SubDocKey &subdoc_key, const QLValuePB& value) {
        KeyValuePairPB *const kv = write_batch->add_write_pairs();
        kv->set_key(subdoc_key.Encode().ToStringBuffer());
        ValueBuffer buffer;
        docdb::AppendEncodedValue(value, SortingType::kNotSpecified, &buffer);
        kv->set_value(buffer.ToStringBuffer());
    };

  std::string hash_key;
  YBPartition::AppendIntToKey<int32_t, uint32_t>(key_val, &hash_key);
  auto hash = YBPartition::HashColumnCompoundValue(hash_key);
  const DocKey doc_key(hash, {PrimitiveValue::Int32(key_val)}, {});
  QLValuePB value;
  value.set_int32_value(int_val);
  add_kv_pair(SubDocKey(doc_key, PrimitiveValue(int_val_col_id)), value);
  value.set_string_value(string_val);
  add_kv_pair(SubDocKey(doc_key, PrimitiveValue(string_val_col_id)), value);
}

}  // namespace yb
