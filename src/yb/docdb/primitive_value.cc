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

#include "yb/docdb/primitive_value.h"

#include <string>

#include <glog/logging.h>

#include "yb/common/ql_type.h"
#include "yb/common/ql_value.h"

#include "yb/common/value.messages.h"

#include "yb/docdb/doc_key.h"
#include "yb/docdb/doc_kv_util.h"
#include "yb/docdb/intent.h"
#include "yb/docdb/value_type.h"

#include "yb/gutil/casts.h"
#include "yb/gutil/macros.h"
#include "yb/gutil/stringprintf.h"
#include "yb/gutil/strings/substitute.h"

#include "yb/util/bytes_formatter.h"
#include "yb/util/compare_util.h"
#include "yb/util/decimal.h"
#include "yb/util/fast_varint.h"
#include "yb/util/net/inetaddress.h"
#include "yb/util/result.h"
#include "yb/util/status_format.h"
#include "yb/util/status_log.h"

using std::string;
using strings::Substitute;
using yb::QLValuePB;
using yb::common::Jsonb;
using yb::util::Decimal;
using yb::util::VarInt;
using yb::FormatBytesAsStr;
using yb::util::CompareUsingLessThan;
using yb::util::FastDecodeSignedVarIntUnsafe;
using yb::util::kInt32SignBitFlipMask;
using yb::util::AppendBigEndianUInt64;
using yb::util::AppendBigEndianUInt32;
using yb::util::DecodeInt64FromKey;
using yb::util::DecodeFloatFromKey;
using yb::util::DecodeDoubleFromKey;

// We're listing all non-primitive value types at the end of switch statement instead of using a
// default clause so that we can ensure that we're handling all possible primitive value types
// at compile time.
#define IGNORE_NON_PRIMITIVE_VALUE_TYPES_IN_SWITCH \
    case ValueType::kArray: FALLTHROUGH_INTENDED; \
    case ValueType::kBitSet: FALLTHROUGH_INTENDED; \
    case ValueType::kExternalIntents: FALLTHROUGH_INTENDED; \
    case ValueType::kGreaterThanIntentType: FALLTHROUGH_INTENDED; \
    case ValueType::kGroupEnd: FALLTHROUGH_INTENDED; \
    case ValueType::kGroupEndDescending: FALLTHROUGH_INTENDED; \
    case ValueType::kInvalid: FALLTHROUGH_INTENDED; \
    case ValueType::kJsonb: FALLTHROUGH_INTENDED; \
    case ValueType::kMergeFlags: FALLTHROUGH_INTENDED; \
    case ValueType::kObject: FALLTHROUGH_INTENDED; \
    case ValueType::kObsoleteIntentPrefix: FALLTHROUGH_INTENDED; \
    case ValueType::kRedisList: FALLTHROUGH_INTENDED;            \
    case ValueType::kRedisSet: FALLTHROUGH_INTENDED; \
    case ValueType::kRedisSortedSet: FALLTHROUGH_INTENDED;  \
    case ValueType::kRedisTS: FALLTHROUGH_INTENDED; \
    case ValueType::kRowLock: FALLTHROUGH_INTENDED; \
    case ValueType::kTombstone: FALLTHROUGH_INTENDED; \
    case ValueType::kTtl: FALLTHROUGH_INTENDED; \
    case ValueType::kUserTimestamp: \
  break

namespace yb {
namespace docdb {

namespace {

template <class T>
string RealToString(T val) {
  string s = std::to_string(val);
  // Remove trailing zeros.
  auto dot_pos = s.find('.');
  if (dot_pos != string::npos) {
    s.erase(std::max(dot_pos + 1, s.find_last_not_of('0')) + 1, string::npos);
  }
  if (s == "0.0" && val != 0.0) {
    // Use the exponential notation for small numbers that would otherwise look like a zero.
    return StringPrintf("%E", val);
  }
  return s;
}

ValueType VirtualValueToValueType(QLVirtualValuePB value) {
  switch (value) {
    case QLVirtualValuePB::LIMIT_MAX:
      return ValueType::kHighest;
    case QLVirtualValuePB::LIMIT_MIN:
      return ValueType::kLowest;
    case QLVirtualValuePB::COUNTER:
      return ValueType::kCounter;
    case QLVirtualValuePB::SS_FORWARD:
      return ValueType::kSSForward;
    case QLVirtualValuePB::SS_REVERSE:
      return ValueType::kSSReverse;
    case QLVirtualValuePB::TOMBSTONE:
      return ValueType::kTombstone;
    case QLVirtualValuePB::NULL_LOW:
      return ValueType::kNullLow;
    case QLVirtualValuePB::ARRAY:
      return ValueType::kArray;
  }
  FATAL_INVALID_ENUM_VALUE(QLVirtualValuePB, value);
}

} // anonymous namespace

const PrimitiveValue PrimitiveValue::kInvalid = PrimitiveValue(ValueType::kInvalid);
const PrimitiveValue PrimitiveValue::kTombstone = PrimitiveValue(ValueType::kTombstone);
const PrimitiveValue PrimitiveValue::kObject = PrimitiveValue(ValueType::kObject);
const PrimitiveValue PrimitiveValue::kLivenessColumn = PrimitiveValue::SystemColumnId(
    SystemColumnIds::kLivenessColumn);

string PrimitiveValue::ToString(AutoDecodeKeys auto_decode_keys) const {
  switch (type_) {
    case ValueType::kNullHigh: FALLTHROUGH_INTENDED;
    case ValueType::kNullLow:
      return "null";
    case ValueType::kGinNull:
      switch (gin_null_val_) {
        // case 0, gin:norm-key, should not exist since the actual data would be used instead.
        case 1:
          return "GinNullKey";
        case 2:
          return "GinEmptyItem";
        case 3:
          return "GinNullItem";
        // case -1, gin:empty-query, should not exist since that's internal to postgres.
        default:
          LOG(FATAL) << "Unexpected gin null category: " << gin_null_val_;
      }
    case ValueType::kCounter:
      return "counter";
    case ValueType::kSSForward:
      return "SSforward";
    case ValueType::kSSReverse:
      return "SSreverse";
    case ValueType::kFalse: FALLTHROUGH_INTENDED;
    case ValueType::kFalseDescending:
      return "false";
    case ValueType::kTrue: FALLTHROUGH_INTENDED;
    case ValueType::kTrueDescending:
      return "true";
    case ValueType::kInvalid:
      return "invalid";
    case ValueType::kCollStringDescending: FALLTHROUGH_INTENDED;
    case ValueType::kCollString: FALLTHROUGH_INTENDED;
    case ValueType::kStringDescending: FALLTHROUGH_INTENDED;
    case ValueType::kString:
      if (auto_decode_keys) {
        // This is useful when logging write batches for secondary indexes.
        SubDocKey sub_doc_key;
        Status decode_status = sub_doc_key.FullyDecodeFrom(str_val_, HybridTimeRequired::kFalse);
        if (decode_status.ok()) {
          // This gives us "EncodedSubDocKey(...)".
          return Format("Encoded$0", sub_doc_key);
        }
      }
      return FormatBytesAsStr(str_val_);
    case ValueType::kInt32Descending: FALLTHROUGH_INTENDED;
    case ValueType::kInt32:
      return std::to_string(int32_val_);
    case ValueType::kUInt32:
    case ValueType::kUInt32Descending:
      return std::to_string(uint32_val_);
    case ValueType::kUInt64:  FALLTHROUGH_INTENDED;
    case ValueType::kUInt64Descending:
      return std::to_string(uint64_val_);
    case ValueType::kInt64Descending: FALLTHROUGH_INTENDED;
    case ValueType::kInt64:
      return std::to_string(int64_val_);
    case ValueType::kFloatDescending: FALLTHROUGH_INTENDED;
    case ValueType::kFloat:
      return RealToString(float_val_);
    case ValueType::kFrozenDescending: FALLTHROUGH_INTENDED;
    case ValueType::kFrozen: {
      std::stringstream ss;
      bool first = true;
      ss << "<";
      for (const auto& pv : *frozen_val_) {
        if (first) {
          first = false;
        } else {
          ss << ",";
        }
        ss << pv.ToString();
      }
      ss << ">";
      return ss.str();
    }
    case ValueType::kDoubleDescending: FALLTHROUGH_INTENDED;
    case ValueType::kDouble:
      return RealToString(double_val_);
    case ValueType::kDecimalDescending: FALLTHROUGH_INTENDED;
    case ValueType::kDecimal: {
      util::Decimal decimal;
      auto status = decimal.DecodeFromComparable(decimal_val_);
      if (!status.ok()) {
        LOG(ERROR) << "Unable to decode decimal";
        return "";
      }
      return decimal.ToString();
    }
    case ValueType::kVarIntDescending: FALLTHROUGH_INTENDED;
    case ValueType::kVarInt: {
      util::VarInt varint;
      auto status = varint.DecodeFromComparable(varint_val_);
      if (!status.ok()) {
        LOG(ERROR) << "Unable to decode varint: " << status.message().ToString();
        return "";
      }
      return varint.ToString();
    }
    case ValueType::kTimestampDescending: FALLTHROUGH_INTENDED;
    case ValueType::kTimestamp:
      return timestamp_val_.ToString();
    case ValueType::kInetaddressDescending: FALLTHROUGH_INTENDED;
    case ValueType::kInetaddress:
      return inetaddress_val_->ToString();
    case ValueType::kJsonb:
      return FormatBytesAsStr(json_val_);
    case ValueType::kUuidDescending: FALLTHROUGH_INTENDED;
    case ValueType::kUuid:
      return uuid_val_.ToString();
    case ValueType::kArrayIndex:
      return Substitute("ArrayIndex($0)", int64_val_);
    case ValueType::kHybridTime:
      return hybrid_time_val_.ToString();
    case ValueType::kUInt16Hash:
      return Substitute("UInt16Hash($0)", uint16_val_);
    case ValueType::kColumnId:
      return Format("ColumnId($0)", column_id_val_);
    case ValueType::kSystemColumnId:
      return Format("SystemColumnId($0)", column_id_val_);
    case ValueType::kObject:
      return "{}";
    case ValueType::kRedisSet:
      return "()";
    case ValueType::kRedisTS:
      return "<>";
    case ValueType::kRedisSortedSet:
      return "(->)";
    case ValueType::kTombstone:
      return "DEL";
    case ValueType::kRedisList: FALLTHROUGH_INTENDED;
    case ValueType::kArray:
      return "[]";
    case ValueType::kTableId:
      return Format("TableId($0)", uuid_val_.ToString());
    case ValueType::kColocationId:
      return Format("ColocationId($0)", uint32_val_);
    case ValueType::kTransactionApplyState: FALLTHROUGH_INTENDED;
    case ValueType::kExternalTransactionId: FALLTHROUGH_INTENDED;
    case ValueType::kTransactionId:
      return Substitute("TransactionId($0)", uuid_val_.ToString());
    case ValueType::kSubTransactionId:
      return Substitute("SubTransactionId($0)", uint32_val_);
    case ValueType::kWriteId:
      return Format("WriteId($0)", int32_val_);
    case ValueType::kIntentTypeSet:
      return Format("Intents($0)", IntentTypeSet(uint16_val_));
    case ValueType::kObsoleteIntentTypeSet:
      return Format("ObsoleteIntents($0)", uint16_val_);
    case ValueType::kObsoleteIntentType:
      return Format("Intent($0)", uint16_val_);
    case ValueType::kMergeFlags: FALLTHROUGH_INTENDED;
    case ValueType::kRowLock: FALLTHROUGH_INTENDED;
    case ValueType::kBitSet: FALLTHROUGH_INTENDED;
    case ValueType::kGroupEnd: FALLTHROUGH_INTENDED;
    case ValueType::kGroupEndDescending: FALLTHROUGH_INTENDED;
    case ValueType::kTtl: FALLTHROUGH_INTENDED;
    case ValueType::kUserTimestamp: FALLTHROUGH_INTENDED;
    case ValueType::kObsoleteIntentPrefix: FALLTHROUGH_INTENDED;
    case ValueType::kExternalIntents: FALLTHROUGH_INTENDED;
    case ValueType::kGreaterThanIntentType:
      break;
    case ValueType::kLowest:
      return "-Inf";
    case ValueType::kHighest:
      return "+Inf";
    case ValueType::kMaxByte:
      return "0xff";
  }
  FATAL_INVALID_ENUM_VALUE(ValueType, type_);
}

void PrimitiveValue::AppendToKey(KeyBytes* key_bytes) const {
  key_bytes->AppendValueType(type_);
  switch (type_) {
    case ValueType::kLowest: return;
    case ValueType::kHighest: return;
    case ValueType::kMaxByte: return;
    case ValueType::kNullHigh: return;
    case ValueType::kNullLow: return;
    case ValueType::kCounter: return;
    case ValueType::kSSForward: return;
    case ValueType::kSSReverse: return;
    case ValueType::kFalse: return;
    case ValueType::kTrue: return;
    case ValueType::kFalseDescending: return;
    case ValueType::kTrueDescending: return;

    case ValueType::kCollString: FALLTHROUGH_INTENDED;
    case ValueType::kString:
      key_bytes->AppendString(str_val_);
      return;

    case ValueType::kCollStringDescending: FALLTHROUGH_INTENDED;
    case ValueType::kStringDescending:
      key_bytes->AppendDescendingString(str_val_);
      return;

    case ValueType::kInt64:
      key_bytes->AppendInt64(int64_val_);
      return;

    case ValueType::kInt32: FALLTHROUGH_INTENDED;
    case ValueType::kWriteId:
      key_bytes->AppendInt32(int32_val_);
      return;

    case ValueType::kColocationId: FALLTHROUGH_INTENDED;
    case ValueType::kSubTransactionId: FALLTHROUGH_INTENDED;
    case ValueType::kUInt32:
      key_bytes->AppendUInt32(uint32_val_);
      return;

    case ValueType::kUInt32Descending:
      key_bytes->AppendDescendingUInt32(uint32_val_);
      return;

    case ValueType::kInt32Descending:
      key_bytes->AppendDescendingInt32(int32_val_);
      return;

    case ValueType::kInt64Descending:
      key_bytes->AppendDescendingInt64(int64_val_);
      return;

    case ValueType::kUInt64:
      key_bytes->AppendUInt64(uint64_val_);
      return;

    case ValueType::kUInt64Descending:
      key_bytes->AppendDescendingUInt64(uint64_val_);
      return;

    case ValueType::kDouble:
      key_bytes->AppendDouble(double_val_);
      return;

    case ValueType::kDoubleDescending:
      key_bytes->AppendDescendingDouble(double_val_);
      return;

    case ValueType::kFloat:
      key_bytes->AppendFloat(float_val_);
      return;

    case ValueType::kFloatDescending:
      key_bytes->AppendDescendingFloat(float_val_);
      return;

    case ValueType::kFrozenDescending: FALLTHROUGH_INTENDED;
    case ValueType::kFrozen: {
      for (const auto& pv : *frozen_val_) {
        pv.AppendToKey(key_bytes);
      }
      if (type_ == ValueType::kFrozenDescending) {
        key_bytes->AppendValueType(ValueType::kGroupEndDescending);
      } else {
        key_bytes->AppendValueType(ValueType::kGroupEnd);
      }
      return;
    }

    case ValueType::kDecimal:
      key_bytes->AppendDecimal(decimal_val_);
      return;

    case ValueType::kDecimalDescending:
      key_bytes->AppendDecimalDescending(decimal_val_);
      return;

    case ValueType::kVarInt:
      key_bytes->AppendVarInt(varint_val_);
      return;

    case ValueType::kVarIntDescending:
      key_bytes->AppendVarIntDescending(varint_val_);
      return;

    case ValueType::kTimestamp:
      key_bytes->AppendInt64(timestamp_val_.ToInt64());
      return;

    case ValueType::kTimestampDescending:
      key_bytes->AppendDescendingInt64(timestamp_val_.ToInt64());
      return;

    case ValueType::kInetaddress: {
      key_bytes->AppendString(inetaddress_val_->ToBytes());
      return;
    }

    case ValueType::kInetaddressDescending: {
      key_bytes->AppendDescendingString(inetaddress_val_->ToBytes());
      return;
    }

    case ValueType::kTransactionApplyState: FALLTHROUGH_INTENDED;
    case ValueType::kExternalTransactionId: FALLTHROUGH_INTENDED;
    case ValueType::kTransactionId: FALLTHROUGH_INTENDED;
    case ValueType::kTableId: FALLTHROUGH_INTENDED;
    case ValueType::kUuid: {
      std::string bytes;
      uuid_val_.EncodeToComparable(&bytes);
      key_bytes->AppendString(bytes);
      return;
    }

    case ValueType::kUuidDescending: {
      std::string bytes;
      uuid_val_.EncodeToComparable(&bytes);
      key_bytes->AppendDescendingString(bytes);
      return;
    }

    case ValueType::kArrayIndex:
      key_bytes->AppendInt64(int64_val_);
      return;

    case ValueType::kHybridTime:
      hybrid_time_val_.AppendEncodedInDocDbFormat(key_bytes->mutable_data());
      return;

    case ValueType::kUInt16Hash:
      key_bytes->AppendUInt16(uint16_val_);
      return;

    case ValueType::kColumnId: FALLTHROUGH_INTENDED;
    case ValueType::kSystemColumnId:
      key_bytes->AppendColumnId(column_id_val_);
      return;

    case ValueType::kObsoleteIntentType:
      key_bytes->AppendIntentTypeSet(ObsoleteIntentTypeToSet(uint16_val_));
      return;

    case ValueType::kObsoleteIntentTypeSet:
      key_bytes->AppendIntentTypeSet(ObsoleteIntentTypeSetToNew(uint16_val_));
      return;

    case ValueType::kIntentTypeSet:
      key_bytes->AppendIntentTypeSet(IntentTypeSet(uint16_val_));
      return;

    case ValueType::kGinNull:
      key_bytes->AppendUint8(gin_null_val_);
      return;

    IGNORE_NON_PRIMITIVE_VALUE_TYPES_IN_SWITCH;
  }
  FATAL_INVALID_ENUM_VALUE(ValueType, type_);
}

namespace {

template <class Buffer>
void AddValueType(
    ValueType ascending, ValueType descending, SortingType sorting_type, Buffer* out) {
  if (sorting_type == SortingType::kDescending ||
      sorting_type == SortingType::kDescendingNullsLast) {
    out->push_back(static_cast<char>(descending));
  } else {
    out->push_back(static_cast<char>(ascending));
  }
}

// Flags for jsonb.
// Indicates that the stored jsonb is the complete jsonb value and not a partial update to jsonb.
static constexpr int64_t kCompleteJsonb = 1;

template <class Buffer>
void DoAppendEncodedValue(const QLValuePB& value, SortingType sorting_type, Buffer* out) {
  switch (value.value_case()) {
    case QLValuePB::kInt8Value:
      AddValueType(ValueType::kInt32, ValueType::kInt32Descending, sorting_type, out);
      AppendBigEndianUInt32(value.int8_value(), out);
      return;
    case QLValuePB::kInt16Value:
      AddValueType(ValueType::kInt32, ValueType::kInt32Descending, sorting_type, out);
      AppendBigEndianUInt32(value.int16_value(), out);
      return;
    case QLValuePB::kInt32Value:
      AddValueType(ValueType::kInt32, ValueType::kInt32Descending, sorting_type, out);
      AppendBigEndianUInt32(value.int32_value(), out);
      return;
    case QLValuePB::kInt64Value:
      AddValueType(ValueType::kInt64, ValueType::kInt64Descending, sorting_type, out);
      AppendBigEndianUInt64(value.int64_value(), out);
      return;
    case QLValuePB::kUint32Value:
      AddValueType(ValueType::kUInt32, ValueType::kUInt32Descending, sorting_type, out);
      AppendBigEndianUInt32(value.uint32_value(), out);
      return;
    case QLValuePB::kUint64Value:
      AddValueType(ValueType::kUInt64, ValueType::kUInt64Descending, sorting_type, out);
      AppendBigEndianUInt64(value.uint64_value(), out);
      return;
    case QLValuePB::kFloatValue:
      AddValueType(ValueType::kFloat, ValueType::kFloatDescending, sorting_type, out);
      AppendBigEndianUInt32(bit_cast<uint32_t>(util::CanonicalizeFloat(value.float_value())), out);
      return;
    case QLValuePB::kDoubleValue:
      AddValueType(ValueType::kDouble, ValueType::kDoubleDescending, sorting_type, out);
      AppendBigEndianUInt64(
          bit_cast<uint64_t>(util::CanonicalizeDouble(value.double_value())), out);
      return;
    case QLValuePB::kDecimalValue:
      AddValueType(ValueType::kDecimal, ValueType::kDecimalDescending, sorting_type, out);
      out->append(value.decimal_value());
      return;
    case QLValuePB::kVarintValue:
      AddValueType(ValueType::kVarInt, ValueType::kVarIntDescending, sorting_type, out);
      out->append(value.varint_value());
      return;
    case QLValuePB::kStringValue: {
      const string& val = value.string_value();
      // In both Postgres and YCQL, character value cannot have embedded \0 byte.
      // Redis allows embedded \0 byte but it does not use QLValuePB so will not
      // come here to pick up 'is_collate'. Therefore, if the value is not empty
      // and the first byte is \0, it indicates this is a collation encoded string.
      if (!val.empty() && val[0] == '\0' && sorting_type != SortingType::kNotSpecified) {
        // An empty collation encoded string is at least 3 bytes.
        CHECK_GE(val.size(), 3);
        AddValueType(ValueType::kCollString, ValueType::kCollStringDescending, sorting_type, out);
      } else {
        AddValueType(ValueType::kString, ValueType::kStringDescending, sorting_type, out);
      }
      out->append(val);
      return;
    }
    case QLValuePB::kBinaryValue:
      AddValueType(ValueType::kString, ValueType::kStringDescending, sorting_type, out);
      out->append(value.binary_value());
      return;
    case QLValuePB::kBoolValue:
      if (value.bool_value()) {
        AddValueType(ValueType::kTrue, ValueType::kTrueDescending, sorting_type, out);
      } else {
        AddValueType(ValueType::kFalse, ValueType::kFalseDescending, sorting_type, out);
      }
      return;
    case QLValuePB::kTimestampValue:
      AddValueType(ValueType::kTimestamp, ValueType::kTimestampDescending, sorting_type, out);
      AppendBigEndianUInt64(QLValue::timestamp_value(value).ToInt64(), out);
      return;
    case QLValuePB::kDateValue:
      AddValueType(ValueType::kUInt32, ValueType::kUInt32Descending, sorting_type, out);
      AppendBigEndianUInt32(value.date_value(), out);
      return;
    case QLValuePB::kTimeValue:
      AddValueType(ValueType::kInt64, ValueType::kInt64Descending, sorting_type, out);
      AppendBigEndianUInt64(value.time_value(), out);
      return;
    case QLValuePB::kInetaddressValue:
      AddValueType(ValueType::kInetaddress, ValueType::kInetaddressDescending, sorting_type, out);
      QLValue::inetaddress_value(value).AppendToBytes(out);
      return;
    case QLValuePB::kJsonbValue:
      out->push_back(ValueTypeAsChar::kJsonb);
      // Append the jsonb flags.
      AppendBigEndianUInt64(kCompleteJsonb, out);
      // Append the jsonb serialized blob.
      out->append(QLValue::jsonb_value(value));
      return;
    case QLValuePB::kUuidValue:
      AddValueType(ValueType::kUuid, ValueType::kUuidDescending, sorting_type, out);
      QLValue::uuid_value(value).AppendEncodedComparable(out);
      return;
    case QLValuePB::kTimeuuidValue:
      AddValueType(ValueType::kUuid, ValueType::kUuidDescending, sorting_type, out);
      QLValue::timeuuid_value(value).AppendEncodedComparable(out);
      return;
    case QLValuePB::kFrozenValue: {
      const QLSeqValuePB& frozen = value.frozen_value();
      AddValueType(ValueType::kFrozen, ValueType::kFrozenDescending, sorting_type, out);
      auto ascending = SortOrderFromColumnSchemaSortingType(sorting_type) == SortOrder::kAscending;
      auto null_value_type = ascending ? ValueType::kNullLow : ValueType::kNullHigh;
      KeyBytes key;
      for (int i = 0; i < frozen.elems_size(); i++) {
        if (IsNull(frozen.elems(i))) {
          key.AppendValueType(null_value_type);
        } else {
          PrimitiveValue::FromQLValuePB(frozen.elems(i), sorting_type).AppendToKey(&key);
        }
      }
      key.AppendValueType(ascending ? ValueType::kGroupEnd : ValueType::kGroupEndDescending);
      out->append(key.data().AsSlice().cdata(), key.data().size());
      return;
    }
    case QLValuePB::VALUE_NOT_SET:
      out->push_back(ValueTypeAsChar::kTombstone);
      return;

    case QLValuePB::kMapValue: FALLTHROUGH_INTENDED;
    case QLValuePB::kSetValue: FALLTHROUGH_INTENDED;
    case QLValuePB::kListValue:
      break;

    case QLValuePB::kVirtualValue:
      out->push_back(static_cast<char>(VirtualValueToValueType(value.virtual_value())));
      return;
    case QLValuePB::kGinNullValue:
      out->push_back(ValueTypeAsChar::kGinNull);
      out->push_back(static_cast<char>(value.gin_null_value()));
      return;
    // default: fall through
  }

  FATAL_INVALID_ENUM_VALUE(QLValuePB::ValueCase, value.value_case());
}

} // namespace

void AppendEncodedValue(const QLValuePB& value, SortingType sorting_type, ValueBuffer* out) {
  DoAppendEncodedValue(value, sorting_type, out);
}

void AppendEncodedValue(const QLValuePB& value, SortingType sorting_type, std::string* out) {
  DoAppendEncodedValue(value, sorting_type, out);
}

Status PrimitiveValue::DecodeFromKey(rocksdb::Slice* slice) {
  return DecodeKey(slice, this);
}

Status PrimitiveValue::DecodeKey(rocksdb::Slice* slice, PrimitiveValue* out) {
  // A copy for error reporting.
  const rocksdb::Slice input_slice(*slice);

  if (slice->empty()) {
    return STATUS_SUBSTITUTE(Corruption,
        "Cannot decode a primitive value in the key encoding format from an empty slice: $0",
        ToShortDebugStr(input_slice));
  }
  ValueType value_type = ConsumeValueType(slice);
  ValueType dummy_type;
  ValueType& type_ref = out ? out->type_ : dummy_type;

  if (out) {
    out->~PrimitiveValue();
    // Ensure we are not leaving the object in an invalid state in case e.g. an exception is thrown
    // due to inability to allocate memory.
  }
  type_ref = ValueType::kNullLow;

  switch (value_type) {
    case ValueType::kNullHigh: FALLTHROUGH_INTENDED;
    case ValueType::kNullLow: FALLTHROUGH_INTENDED;
    case ValueType::kCounter: FALLTHROUGH_INTENDED;
    case ValueType::kSSForward: FALLTHROUGH_INTENDED;
    case ValueType::kSSReverse: FALLTHROUGH_INTENDED;
    case ValueType::kFalse: FALLTHROUGH_INTENDED;
    case ValueType::kTrue: FALLTHROUGH_INTENDED;
    case ValueType::kFalseDescending: FALLTHROUGH_INTENDED;
    case ValueType::kTrueDescending: FALLTHROUGH_INTENDED;
    case ValueType::kHighest: FALLTHROUGH_INTENDED;
    case ValueType::kLowest:
      type_ref = value_type;
      return Status::OK();

    case ValueType::kCollStringDescending:  FALLTHROUGH_INTENDED;
    case ValueType::kStringDescending: {
      if (out) {
        string result;
        RETURN_NOT_OK(DecodeComplementZeroEncodedStr(slice, &result));
        new (&out->str_val_) string(std::move(result));
      } else {
        RETURN_NOT_OK(DecodeComplementZeroEncodedStr(slice, nullptr));
      }
      // Only set type to string after string field initialization succeeds.
      type_ref = value_type;
      return Status::OK();
    }

    case ValueType::kCollString: FALLTHROUGH_INTENDED;
    case ValueType::kString: {
      if (out) {
        string result;
        RETURN_NOT_OK(DecodeZeroEncodedStr(slice, &result));
        new (&out->str_val_) string(std::move(result));
      } else {
        RETURN_NOT_OK(DecodeZeroEncodedStr(slice, nullptr));
      }
      // Only set type to string after string field initialization succeeds.
      type_ref = value_type;
      return Status::OK();
    }

    case ValueType::kFrozenDescending:
    case ValueType::kFrozen: {
      ValueType end_marker_value_type = ValueType::kGroupEnd;
      if (value_type == ValueType::kFrozenDescending) {
        end_marker_value_type = ValueType::kGroupEndDescending;
      }

      if (out) {
        out->frozen_val_ = new FrozenContainer();
        while (!slice->empty()) {
          ValueType current_value_type = static_cast<ValueType>(*slice->data());
          if (current_value_type == end_marker_value_type) {
            slice->consume_byte();
            type_ref = value_type;
            return Status::OK();
          } else {
            PrimitiveValue pv;
            RETURN_NOT_OK(DecodeKey(slice, &pv));
            out->frozen_val_->push_back(pv);
          }
        }
      } else {
        while (!slice->empty()) {
          ValueType current_value_type = static_cast<ValueType>(*slice->data());
          if (current_value_type == end_marker_value_type) {
            slice->consume_byte();
            return Status::OK();
          } else {
            RETURN_NOT_OK(DecodeKey(slice, nullptr));
          }
        }
      }

      return STATUS(Corruption, "Reached end of slice looking for frozen group end marker");
    }

    case ValueType::kDecimalDescending: FALLTHROUGH_INTENDED;
    case ValueType::kDecimal: {
      util::Decimal decimal;
      Slice slice_temp(slice->data(), slice->size());
      size_t num_decoded_bytes = 0;
      RETURN_NOT_OK(decimal.DecodeFromComparable(slice_temp, &num_decoded_bytes));
      if (value_type == ValueType::kDecimalDescending) {
        // When we encode a descending decimal, we do a bitwise negation of each byte, which changes
        // the sign of the number. This way we reverse the sorting order. decimal.Negate() restores
        // the original sign of the number.
        decimal.Negate();
      }
      if (out) { // TODO avoid using temp variable, when out is nullptr
        new(&out->decimal_val_) string(decimal.EncodeToComparable());
      }
      slice->remove_prefix(num_decoded_bytes);
      type_ref = value_type;
      return Status::OK();
    }

    case ValueType::kVarIntDescending: FALLTHROUGH_INTENDED;
    case ValueType::kVarInt: {
      util::VarInt varint;
      Slice slice_temp(slice->data(), slice->size());
      size_t num_decoded_bytes = 0;
      RETURN_NOT_OK(varint.DecodeFromComparable(slice_temp, &num_decoded_bytes));
      if (value_type == ValueType::kVarIntDescending) {
        varint.Negate();
      }
      if (out) { // TODO avoid using temp variable, when out is nullptr
        new(&out->varint_val_) string(varint.EncodeToComparable());
      }
      slice->remove_prefix(num_decoded_bytes);
      type_ref = value_type;
      return Status::OK();
    }

    case ValueType::kGinNull: {
      if (slice->size() < sizeof(uint8_t)) {
        return STATUS_SUBSTITUTE(Corruption,
                                 "Not enough bytes to decode an 8-bit integer: $0",
                                 slice->size());
      }
      if (out) {
        out->gin_null_val_ = slice->data()[0];
      }
      slice->remove_prefix(sizeof(uint8_t));
      type_ref = value_type;
      return Status::OK();
    }

    case ValueType::kInt32Descending: FALLTHROUGH_INTENDED;
    case ValueType::kInt32: FALLTHROUGH_INTENDED;
    case ValueType::kWriteId:
      if (slice->size() < sizeof(int32_t)) {
        return STATUS_SUBSTITUTE(Corruption,
                                 "Not enough bytes to decode a 32-bit integer: $0",
                                 slice->size());
      }
      if (out) {
        out->int32_val_ = BigEndian::Load32(slice->data()) ^ kInt32SignBitFlipMask;
        if (value_type == ValueType::kInt32Descending) {
          out->int32_val_ = ~out->int32_val_;
        }
      }
      slice->remove_prefix(sizeof(int32_t));
      type_ref = value_type;
      return Status::OK();

    case ValueType::kColocationId: FALLTHROUGH_INTENDED;
    case ValueType::kUInt32Descending: FALLTHROUGH_INTENDED;
    case ValueType::kSubTransactionId: FALLTHROUGH_INTENDED;
    case ValueType::kUInt32:
      if (slice->size() < sizeof(uint32_t)) {
        return STATUS_SUBSTITUTE(Corruption,
                                 "Not enough bytes to decode a 32-bit integer: $0",
                                 slice->size());
      }
      if (out) {
        out->uint32_val_ = BigEndian::Load32(slice->data());
        if (value_type == ValueType::kUInt32Descending) {
          out->uint32_val_ = ~out->uint32_val_;
        }
      }
      slice->remove_prefix(sizeof(uint32_t));
      type_ref = value_type;
      return Status::OK();

    case ValueType::kUInt64Descending: FALLTHROUGH_INTENDED;
    case ValueType::kUInt64:
      if (slice->size() < sizeof(uint64_t)) {
        return STATUS_SUBSTITUTE(Corruption,
                                 "Not enough bytes to decode a 64-bit integer: $0",
                                 slice->size());
      }
      if (out) {
        out->uint64_val_ = BigEndian::Load64(slice->data());
        if (value_type == ValueType::kUInt64Descending) {
          out->uint64_val_ = ~out->uint64_val_;
        }
      }
      slice->remove_prefix(sizeof(uint64_t));
      type_ref = value_type;
      return Status::OK();

    case ValueType::kInt64Descending: FALLTHROUGH_INTENDED;
    case ValueType::kInt64: FALLTHROUGH_INTENDED;
    case ValueType::kArrayIndex:
      if (slice->size() < sizeof(int64_t)) {
        return STATUS_SUBSTITUTE(Corruption,
            "Not enough bytes to decode a 64-bit integer: $0",
            slice->size());
      }
      if (out) {
        out->int64_val_ = DecodeInt64FromKey(*slice);
        if (value_type == ValueType::kInt64Descending) {
          out->int64_val_ = ~out->int64_val_;
        }
      }
      slice->remove_prefix(sizeof(int64_t));
      type_ref = value_type;
      return Status::OK();

    case ValueType::kUInt16Hash:
      if (slice->size() < sizeof(uint16_t)) {
        return STATUS(Corruption, Substitute("Not enough bytes to decode a 16-bit hash: $0",
                                             slice->size()));
      }
      if (out) {
        out->uint16_val_ = BigEndian::Load16(slice->data());
      }
      slice->remove_prefix(sizeof(uint16_t));
      type_ref = value_type;
      return Status::OK();

    case ValueType::kTimestampDescending: FALLTHROUGH_INTENDED;
    case ValueType::kTimestamp: {
      if (slice->size() < sizeof(Timestamp)) {
        return STATUS(Corruption,
            Substitute("Not enough bytes to decode a Timestamp: $0, need $1",
                slice->size(), sizeof(Timestamp)));
      }
      if (out) {
        const auto uint64_timestamp = DecodeInt64FromKey(*slice);
        if (value_type == ValueType::kTimestampDescending) {
          // Flip all the bits after loading the integer.
          out->timestamp_val_ = Timestamp(~uint64_timestamp);
        } else {
          out->timestamp_val_ = Timestamp(uint64_timestamp);
        }
      }
      slice->remove_prefix(sizeof(Timestamp));
      type_ref = value_type;
      return Status::OK();
    }

    case ValueType::kInetaddress: {
      if (out) {
        string bytes;
        RETURN_NOT_OK(DecodeZeroEncodedStr(slice, &bytes));
        out->inetaddress_val_ = new InetAddress();
        RETURN_NOT_OK(out->inetaddress_val_->FromBytes(bytes));
      } else {
        RETURN_NOT_OK(DecodeZeroEncodedStr(slice, nullptr));
      }
      type_ref = value_type;
      return Status::OK();
    }

    case ValueType::kInetaddressDescending: {
      if (out) {
        string bytes;
        RETURN_NOT_OK(DecodeComplementZeroEncodedStr(slice, &bytes));
        out->inetaddress_val_ = new InetAddress();
        RETURN_NOT_OK(out->inetaddress_val_->FromBytes(bytes));
      } else {
        RETURN_NOT_OK(DecodeComplementZeroEncodedStr(slice, nullptr));
      }
      type_ref = value_type;
      return Status::OK();
    }

    case ValueType::kTransactionApplyState: FALLTHROUGH_INTENDED;
    case ValueType::kExternalTransactionId:
      if (slice->size() < boost::uuids::uuid::static_size()) {
        return STATUS_FORMAT(Corruption, "Not enough bytes for UUID: $0", slice->size());
      }
      if (out) {
        RETURN_NOT_OK((new(&out->uuid_val_) Uuid())->FromSlice(
            *slice, boost::uuids::uuid::static_size()));
      }
      slice->remove_prefix(boost::uuids::uuid::static_size());
      type_ref = value_type;
      return Status::OK();

    case ValueType::kTransactionId: FALLTHROUGH_INTENDED;
    case ValueType::kTableId: FALLTHROUGH_INTENDED;
    case ValueType::kUuid: {
      if (out) {
        string bytes;
        RETURN_NOT_OK(DecodeZeroEncodedStr(slice, &bytes));
        new(&out->uuid_val_) Uuid();
        RETURN_NOT_OK(out->uuid_val_.DecodeFromComparable(bytes));
      } else {
        RETURN_NOT_OK(DecodeZeroEncodedStr(slice, nullptr));
      }
      type_ref = value_type;
      return Status::OK();
    }

    case ValueType::kUuidDescending: {
      if (out) {
        string bytes;
        RETURN_NOT_OK(DecodeComplementZeroEncodedStr(slice, &bytes));
        new(&out->uuid_val_) Uuid();
        RETURN_NOT_OK(out->uuid_val_.DecodeFromComparable(bytes));
      } else {
        RETURN_NOT_OK(DecodeComplementZeroEncodedStr(slice, nullptr));
      }
      type_ref = value_type;
      return Status::OK();
    }

    case ValueType::kColumnId: FALLTHROUGH_INTENDED;
    case ValueType::kSystemColumnId: {
      // Decode varint
      {
        ColumnId dummy_column_id;
        ColumnId& column_id_ref = out ? out->column_id_val_ : dummy_column_id;
        int64_t column_id_as_int64 = VERIFY_RESULT(FastDecodeSignedVarIntUnsafe(slice));
        RETURN_NOT_OK(ColumnId::FromInt64(column_id_as_int64, &column_id_ref));
      }

      type_ref = value_type;
      return Status::OK();
    }

    case ValueType::kHybridTime: {
      if (out) {
        new (&out->hybrid_time_val_) DocHybridTime(VERIFY_RESULT(DocHybridTime::DecodeFrom(slice)));
      } else {
        RETURN_NOT_OK(DocHybridTime::DecodeFrom(slice));
      }

      type_ref = ValueType::kHybridTime;
      return Status::OK();
    }

    case ValueType::kIntentTypeSet: FALLTHROUGH_INTENDED;
    case ValueType::kObsoleteIntentTypeSet: FALLTHROUGH_INTENDED;
    case ValueType::kObsoleteIntentType: {
      if (out) {
        out->uint16_val_ = static_cast<uint16_t>(*slice->data());
      }
      type_ref = value_type;
      slice->consume_byte();
      return Status::OK();
    }

    case ValueType::kFloatDescending: FALLTHROUGH_INTENDED;
    case ValueType::kFloat: {
      if (slice->size() < sizeof(float_t)) {
        return STATUS_FORMAT(Corruption, "Not enough bytes to decode a float: $0", slice->size());
      }
      if (out) {
        if (value_type == ValueType::kFloatDescending) {
          out->float_val_ = DecodeFloatFromKey(*slice, /* descending */ true);
        } else {
          out->float_val_ = DecodeFloatFromKey(*slice);
        }
      }
      slice->remove_prefix(sizeof(float_t));
      type_ref = value_type;
      return Status::OK();
    }
    case ValueType::kDoubleDescending: FALLTHROUGH_INTENDED;
    case ValueType::kDouble: {
      if (slice->size() < sizeof(double_t)) {
        return STATUS_FORMAT(Corruption, "Not enough bytes to decode a float: $0", slice->size());
      }
      if (out) {
        if (value_type == ValueType::kDoubleDescending) {
          out->double_val_ = DecodeDoubleFromKey(*slice, /* descending */ true);
        } else {
          out->double_val_ = DecodeDoubleFromKey(*slice);
        }
      }
      slice->remove_prefix(sizeof(double_t));
      type_ref = value_type;
      return Status::OK();
    }
    case ValueType::kMaxByte:
      break;

    IGNORE_NON_PRIMITIVE_VALUE_TYPES_IN_SWITCH;
  }
  return STATUS_FORMAT(
      Corruption,
      "Cannot decode value type $0 from the key encoding format: $1",
      value_type,
      ToShortDebugStr(input_slice));
}

Status PrimitiveValue::DecodeFromValue(const rocksdb::Slice& rocksdb_slice) {
  if (rocksdb_slice.empty()) {
    return STATUS(Corruption, "Cannot decode a value from an empty slice");
  }
  rocksdb::Slice slice(rocksdb_slice);
  this->~PrimitiveValue();
  // Ensure we are not leaving the object in an invalid state in case e.g. an exception is thrown
  // due to inability to allocate memory.
  type_ = ValueType::kNullLow;

  const auto value_type = ConsumeValueType(&slice);

  // TODO: ensure we consume all data from the given slice.
  switch (value_type) {
    case ValueType::kNullHigh: FALLTHROUGH_INTENDED;
    case ValueType::kNullLow: FALLTHROUGH_INTENDED;
    case ValueType::kCounter: FALLTHROUGH_INTENDED;
    case ValueType::kSSForward: FALLTHROUGH_INTENDED;
    case ValueType::kSSReverse: FALLTHROUGH_INTENDED;
    case ValueType::kFalse: FALLTHROUGH_INTENDED;
    case ValueType::kTrue: FALLTHROUGH_INTENDED;
    case ValueType::kFalseDescending: FALLTHROUGH_INTENDED;
    case ValueType::kTrueDescending: FALLTHROUGH_INTENDED;
    case ValueType::kObject: FALLTHROUGH_INTENDED;
    case ValueType::kArray: FALLTHROUGH_INTENDED;
    case ValueType::kRedisList: FALLTHROUGH_INTENDED;
    case ValueType::kRedisSet: FALLTHROUGH_INTENDED;
    case ValueType::kRedisTS: FALLTHROUGH_INTENDED;
    case ValueType::kRedisSortedSet: FALLTHROUGH_INTENDED;
    case ValueType::kTombstone:
      type_ = value_type;
      complex_data_structure_ = nullptr;
      return Status::OK();

    case ValueType::kFrozenDescending: FALLTHROUGH_INTENDED;
    case ValueType::kFrozen: {
      ValueType end_marker_value_type = ValueType::kGroupEnd;
      if (value_type == ValueType::kFrozenDescending) {
        end_marker_value_type = ValueType::kGroupEndDescending;
      }

      frozen_val_ = new FrozenContainer();
      while (!slice.empty()) {
        ValueType current_value_type = static_cast<ValueType>(*slice.data());
        if (current_value_type == end_marker_value_type) {
          slice.consume_byte();
          type_ = value_type;
          return Status::OK();
        } else {
          PrimitiveValue pv;
          // Frozen elems are encoded as keys even in values.
          RETURN_NOT_OK(pv.DecodeFromKey(&slice));
          frozen_val_->push_back(pv);
        }
      }

      return STATUS(Corruption, "Reached end of slice looking for frozen group end marker");
    }
    case ValueType::kCollString: FALLTHROUGH_INTENDED;
    case ValueType::kString:
      new(&str_val_) string(slice.cdata(), slice.size());
      // Only set type to string after string field initialization succeeds.
      type_ = value_type;
      return Status::OK();

    case ValueType::kGinNull:
      if (slice.size() != sizeof(uint8_t)) {
        return STATUS_FORMAT(Corruption, "Invalid number of bytes for a $0: $1",
            value_type, slice.size());
      }
      type_ = value_type;
      gin_null_val_ = slice.data()[0];
      return Status::OK();

    case ValueType::kInt32: FALLTHROUGH_INTENDED;
    case ValueType::kInt32Descending: FALLTHROUGH_INTENDED;
    case ValueType::kFloatDescending: FALLTHROUGH_INTENDED;
    case ValueType::kWriteId: FALLTHROUGH_INTENDED;
    case ValueType::kFloat:
      if (slice.size() != sizeof(int32_t)) {
        return STATUS_FORMAT(Corruption, "Invalid number of bytes for a $0: $1",
            value_type, slice.size());
      }
      type_ = value_type;
      int32_val_ = BigEndian::Load32(slice.data());
      return Status::OK();

    case ValueType::kColocationId: FALLTHROUGH_INTENDED;
    case ValueType::kUInt32: FALLTHROUGH_INTENDED;
    case ValueType::kSubTransactionId: FALLTHROUGH_INTENDED;
    case ValueType::kUInt32Descending:
      if (slice.size() != sizeof(uint32_t)) {
        return STATUS_FORMAT(Corruption, "Invalid number of bytes for a $0: $1",
            value_type, slice.size());
      }
      type_ = value_type;
      uint32_val_ = BigEndian::Load32(slice.data());
      return Status::OK();

    case ValueType::kUInt64: FALLTHROUGH_INTENDED;
    case ValueType::kUInt64Descending:
      if (slice.size() != sizeof(uint64_t)) {
        return STATUS_FORMAT(Corruption, "Invalid number of bytes for a $0: $1",
            value_type, slice.size());
      }
      type_ = value_type;
      uint64_val_ = BigEndian::Load64(slice.data());
      return Status::OK();

    case ValueType::kInt64: FALLTHROUGH_INTENDED;
    case ValueType::kInt64Descending: FALLTHROUGH_INTENDED;
    case ValueType::kArrayIndex: FALLTHROUGH_INTENDED;
    case ValueType::kDoubleDescending: FALLTHROUGH_INTENDED;
    case ValueType::kDouble:
      if (slice.size() != sizeof(int64_t)) {
        return STATUS_FORMAT(Corruption, "Invalid number of bytes for a $0: $1",
            value_type, slice.size());
      }
      type_ = value_type;
      int64_val_ = BigEndian::Load64(slice.data());
      return Status::OK();

    case ValueType::kDecimal: {
      util::Decimal decimal;
      size_t num_decoded_bytes = 0;
      RETURN_NOT_OK(decimal.DecodeFromComparable(slice.ToString(), &num_decoded_bytes));
      type_ = value_type;
      new(&decimal_val_) string(decimal.EncodeToComparable());
      return Status::OK();
    }

    case ValueType::kVarInt: {
      util::VarInt varint;
      size_t num_decoded_bytes = 0;
      RETURN_NOT_OK(varint.DecodeFromComparable(slice.ToString(), &num_decoded_bytes));
      type_ = value_type;
      new(&varint_val_) string(varint.EncodeToComparable());
      return Status::OK();
    }

    case ValueType::kTimestamp:
      if (slice.size() != sizeof(Timestamp)) {
        return STATUS_FORMAT(Corruption, "Invalid number of bytes for a $0: $1",
            value_type, slice.size());
      }
      type_ = value_type;
      timestamp_val_ = Timestamp(BigEndian::Load64(slice.data()));
      return Status::OK();

    case ValueType::kJsonb: {
      if (slice.size() < sizeof(int64_t)) {
        return STATUS_FORMAT(Corruption, "Invalid number of bytes for a $0: $1",
                             value_type, slice.size());
      }
      // Read the jsonb flags.
      int64_t jsonb_flags = BigEndian::Load64(slice.data());
      slice.remove_prefix(sizeof(jsonb_flags));

      // Read the serialized jsonb.
      new(&json_val_) string(slice.ToBuffer());
      type_ = value_type;
      return Status::OK();
    }

    case ValueType::kInetaddress: {
      if (slice.size() != kInetAddressV4Size && slice.size() != kInetAddressV6Size) {
        return STATUS_FORMAT(Corruption,
                             "Invalid number of bytes to decode IPv4/IPv6: $0, need $1 or $2",
                             slice.size(), kInetAddressV4Size, kInetAddressV6Size);
      }
      // Need to use a non-rocksdb slice for InetAddress.
      Slice slice_temp(slice.data(), slice.size());
      inetaddress_val_ = new InetAddress();
          RETURN_NOT_OK(inetaddress_val_->FromSlice(slice_temp));
      type_ = value_type;
      return Status::OK();
    }

    case ValueType::kTransactionApplyState: FALLTHROUGH_INTENDED;
    case ValueType::kExternalTransactionId: FALLTHROUGH_INTENDED;
    case ValueType::kTransactionId: FALLTHROUGH_INTENDED;
    case ValueType::kTableId: FALLTHROUGH_INTENDED;
    case ValueType::kUuid: {
      if (slice.size() != kUuidSize) {
        return STATUS_FORMAT(Corruption, "Invalid number of bytes to decode Uuid: $0, need $1",
            slice.size(), kUuidSize);
      }
      Slice slice_temp(slice.data(), slice.size());
      new(&uuid_val_) Uuid();
      RETURN_NOT_OK(uuid_val_.DecodeFromComparableSlice(slice_temp));
      type_ = value_type;
      return Status::OK();
    }

    case ValueType::kIntentTypeSet: FALLTHROUGH_INTENDED;
    case ValueType::kObsoleteIntentTypeSet: FALLTHROUGH_INTENDED;
    case ValueType::kObsoleteIntentType: FALLTHROUGH_INTENDED;
    case ValueType::kGroupEnd: FALLTHROUGH_INTENDED;
    case ValueType::kGroupEndDescending: FALLTHROUGH_INTENDED;
    case ValueType::kObsoleteIntentPrefix: FALLTHROUGH_INTENDED;
    case ValueType::kGreaterThanIntentType: FALLTHROUGH_INTENDED;
    case ValueType::kUInt16Hash: FALLTHROUGH_INTENDED;
    case ValueType::kInvalid: FALLTHROUGH_INTENDED;
    case ValueType::kMergeFlags: FALLTHROUGH_INTENDED;
    case ValueType::kRowLock: FALLTHROUGH_INTENDED;
    case ValueType::kBitSet: FALLTHROUGH_INTENDED;
    case ValueType::kTtl: FALLTHROUGH_INTENDED;
    case ValueType::kUserTimestamp: FALLTHROUGH_INTENDED;
    case ValueType::kColumnId: FALLTHROUGH_INTENDED;
    case ValueType::kSystemColumnId: FALLTHROUGH_INTENDED;
    case ValueType::kHybridTime: FALLTHROUGH_INTENDED;
    case ValueType::kCollStringDescending: FALLTHROUGH_INTENDED;
    case ValueType::kStringDescending: FALLTHROUGH_INTENDED;
    case ValueType::kInetaddressDescending: FALLTHROUGH_INTENDED;
    case ValueType::kDecimalDescending: FALLTHROUGH_INTENDED;
    case ValueType::kVarIntDescending: FALLTHROUGH_INTENDED;
    case ValueType::kUuidDescending: FALLTHROUGH_INTENDED;
    case ValueType::kTimestampDescending: FALLTHROUGH_INTENDED;
    case ValueType::kExternalIntents: FALLTHROUGH_INTENDED;
    case ValueType::kLowest: FALLTHROUGH_INTENDED;
    case ValueType::kHighest: FALLTHROUGH_INTENDED;
    case ValueType::kMaxByte:
      return STATUS_FORMAT(Corruption, "$0 is not allowed in a RocksDB PrimitiveValue", value_type);
  }
  FATAL_INVALID_ENUM_VALUE(ValueType, value_type);
  return Status::OK();
}

PrimitiveValue PrimitiveValue::Double(double d, SortOrder sort_order) {
  PrimitiveValue primitive_value;
  if (sort_order == SortOrder::kAscending) {
    primitive_value.type_ = ValueType::kDouble;
  } else {
    primitive_value.type_ = ValueType::kDoubleDescending;
  }
  primitive_value.double_val_ = d;
  return primitive_value;
}

PrimitiveValue PrimitiveValue::Float(float f, SortOrder sort_order) {
  PrimitiveValue primitive_value;
  if (sort_order == SortOrder::kAscending) {
    primitive_value.type_ = ValueType::kFloat;
  } else {
    primitive_value.type_ = ValueType::kFloatDescending;
  }
  primitive_value.float_val_ = f;
  return primitive_value;
}

PrimitiveValue PrimitiveValue::Decimal(const Slice& decimal_str, SortOrder sort_order) {
  PrimitiveValue primitive_value;
  if (sort_order == SortOrder::kDescending) {
    primitive_value.type_ = ValueType::kDecimalDescending;
  } else {
    primitive_value.type_ = ValueType::kDecimal;
  }
  new(&primitive_value.decimal_val_) string(decimal_str.cdata(), decimal_str.size());
  return primitive_value;
}

PrimitiveValue PrimitiveValue::VarInt(const Slice& varint_str, SortOrder sort_order) {
  PrimitiveValue primitive_value;
  if (sort_order == SortOrder::kDescending) {
    primitive_value.type_ = ValueType::kVarIntDescending;
  } else {
    primitive_value.type_ = ValueType::kVarInt;
  }
  new(&primitive_value.varint_val_) string(varint_str.cdata(), varint_str.size());
  return primitive_value;
}

PrimitiveValue PrimitiveValue::ArrayIndex(int64_t index) {
  PrimitiveValue primitive_value;
  primitive_value.type_ = ValueType::kArrayIndex;
  primitive_value.int64_val_ = index;
  return primitive_value;
}

PrimitiveValue PrimitiveValue::UInt16Hash(uint16_t hash) {
  PrimitiveValue primitive_value;
  primitive_value.type_ = ValueType::kUInt16Hash;
  primitive_value.uint16_val_ = hash;
  return primitive_value;
}

PrimitiveValue PrimitiveValue::SystemColumnId(SystemColumnIds system_column_id) {
  return PrimitiveValue::SystemColumnId(ColumnId(static_cast<ColumnIdRep>(system_column_id)));
}

PrimitiveValue PrimitiveValue::SystemColumnId(ColumnId column_id) {
  PrimitiveValue primitive_value;
  primitive_value.type_ = ValueType::kSystemColumnId;
  primitive_value.column_id_val_ = column_id;
  return primitive_value;
}

PrimitiveValue PrimitiveValue::Int32(int32_t v, SortOrder sort_order) {
  PrimitiveValue primitive_value;
  if (sort_order == SortOrder::kDescending) {
    primitive_value.type_ = ValueType::kInt32Descending;
  } else {
    primitive_value.type_ = ValueType::kInt32;
  }
  primitive_value.int32_val_ = v;
  return primitive_value;
}

PrimitiveValue PrimitiveValue::UInt32(uint32_t v, SortOrder sort_order) {
  PrimitiveValue primitive_value;
  if (sort_order == SortOrder::kDescending) {
    primitive_value.type_ = ValueType::kUInt32Descending;
  } else {
    primitive_value.type_ = ValueType::kUInt32;
  }
  primitive_value.uint32_val_ = v;
  return primitive_value;
}

PrimitiveValue PrimitiveValue::UInt64(uint64_t v, SortOrder sort_order) {
  PrimitiveValue primitive_value;
  if (sort_order == SortOrder::kDescending) {
    primitive_value.type_ = ValueType::kUInt64Descending;
  } else {
    primitive_value.type_ = ValueType::kUInt64;
  }
  primitive_value.uint64_val_ = v;
  return primitive_value;
}

PrimitiveValue PrimitiveValue::TransactionId(Uuid transaction_id) {
  PrimitiveValue primitive_value(transaction_id);
  primitive_value.type_ = ValueType::kTransactionId;
  return primitive_value;
}

PrimitiveValue PrimitiveValue::TableId(Uuid table_id) {
  PrimitiveValue primitive_value(table_id);
  primitive_value.type_ = ValueType::kTableId;
  return primitive_value;
}

PrimitiveValue PrimitiveValue::ColocationId(const yb::ColocationId colocation_id) {
  PrimitiveValue primitive_value(colocation_id);
  primitive_value.type_ = ValueType::kColocationId;
  return primitive_value;
}

PrimitiveValue PrimitiveValue::Jsonb(const Slice& json) {
  PrimitiveValue primitive_value;
  primitive_value.type_ = ValueType::kJsonb;
  new(&primitive_value.json_val_) string(json.cdata(), json.size());
  return primitive_value;
}

PrimitiveValue PrimitiveValue::GinNull(uint8_t v) {
  PrimitiveValue primitive_value;
  primitive_value.type_ = ValueType::kGinNull;
  primitive_value.gin_null_val_ = v;
  return primitive_value;
}

KeyBytes PrimitiveValue::ToKeyBytes() const {
  KeyBytes kb;
  AppendToKey(&kb);
  return kb;
}

bool PrimitiveValue::operator==(const PrimitiveValue& other) const {
  if (type_ != other.type_) {
    return false;
  }
  switch (type_) {
    case ValueType::kNullHigh: FALLTHROUGH_INTENDED;
    case ValueType::kNullLow: FALLTHROUGH_INTENDED;
    case ValueType::kCounter: FALLTHROUGH_INTENDED;
    case ValueType::kFalse: FALLTHROUGH_INTENDED;
    case ValueType::kFalseDescending: FALLTHROUGH_INTENDED;
    case ValueType::kSSForward: FALLTHROUGH_INTENDED;
    case ValueType::kSSReverse: FALLTHROUGH_INTENDED;
    case ValueType::kTrue: FALLTHROUGH_INTENDED;
    case ValueType::kTrueDescending: FALLTHROUGH_INTENDED;
    case ValueType::kLowest: FALLTHROUGH_INTENDED;
    case ValueType::kHighest: FALLTHROUGH_INTENDED;
    case ValueType::kMaxByte: return true;

    case ValueType::kCollStringDescending: FALLTHROUGH_INTENDED;
    case ValueType::kCollString: FALLTHROUGH_INTENDED;
    case ValueType::kStringDescending: FALLTHROUGH_INTENDED;
    case ValueType::kString: return str_val_ == other.str_val_;

    case ValueType::kFrozenDescending: FALLTHROUGH_INTENDED;
    case ValueType::kFrozen: return *frozen_val_ == *other.frozen_val_;

    case ValueType::kInt32Descending: FALLTHROUGH_INTENDED;
    case ValueType::kWriteId: FALLTHROUGH_INTENDED;
    case ValueType::kInt32: return int32_val_ == other.int32_val_;

    case ValueType::kColocationId: FALLTHROUGH_INTENDED;
    case ValueType::kUInt32Descending: FALLTHROUGH_INTENDED;
    case ValueType::kSubTransactionId: FALLTHROUGH_INTENDED;
    case ValueType::kUInt32: return uint32_val_ == other.uint32_val_;

    case ValueType::kUInt64Descending: FALLTHROUGH_INTENDED;
    case ValueType::kUInt64: return uint64_val_ == other.uint64_val_;

    case ValueType::kInt64Descending: FALLTHROUGH_INTENDED;
    case ValueType::kInt64: FALLTHROUGH_INTENDED;
    case ValueType::kArrayIndex: return int64_val_ == other.int64_val_;

    case ValueType::kFloatDescending: FALLTHROUGH_INTENDED;
    case ValueType::kFloat: {
      if (util::IsNanFloat(float_val_) && util::IsNanFloat(other.float_val_)) {
        return true;
      }
      return float_val_ == other.float_val_;
    }
    case ValueType::kDoubleDescending: FALLTHROUGH_INTENDED;
    case ValueType::kDouble: {
      if (util::IsNanDouble(double_val_) && util::IsNanDouble(other.double_val_)) {
        return true;
      }
      return double_val_ == other.double_val_;
    }
    case ValueType::kDecimalDescending: FALLTHROUGH_INTENDED;
    case ValueType::kDecimal: return decimal_val_ == other.decimal_val_;
    case ValueType::kVarIntDescending: FALLTHROUGH_INTENDED;
    case ValueType::kVarInt: return varint_val_ == other.varint_val_;
    case ValueType::kIntentTypeSet: FALLTHROUGH_INTENDED;
    case ValueType::kObsoleteIntentTypeSet: FALLTHROUGH_INTENDED;
    case ValueType::kObsoleteIntentType: FALLTHROUGH_INTENDED;
    case ValueType::kUInt16Hash: return uint16_val_ == other.uint16_val_;

    case ValueType::kTimestampDescending: FALLTHROUGH_INTENDED;
    case ValueType::kTimestamp: return timestamp_val_ == other.timestamp_val_;
    case ValueType::kInetaddressDescending: FALLTHROUGH_INTENDED;
    case ValueType::kInetaddress: return *inetaddress_val_ == *(other.inetaddress_val_);
    case ValueType::kTransactionApplyState: FALLTHROUGH_INTENDED;
    case ValueType::kExternalTransactionId: FALLTHROUGH_INTENDED;
    case ValueType::kTransactionId: FALLTHROUGH_INTENDED;
    case ValueType::kTableId: FALLTHROUGH_INTENDED;
    case ValueType::kUuidDescending: FALLTHROUGH_INTENDED;
    case ValueType::kUuid: return uuid_val_ == other.uuid_val_;

    case ValueType::kColumnId: FALLTHROUGH_INTENDED;
    case ValueType::kSystemColumnId: return column_id_val_ == other.column_id_val_;
    case ValueType::kHybridTime: return hybrid_time_val_.CompareTo(other.hybrid_time_val_) == 0;
    case ValueType::kGinNull: return gin_null_val_ == other.gin_null_val_;
    IGNORE_NON_PRIMITIVE_VALUE_TYPES_IN_SWITCH;
  }
  FATAL_INVALID_ENUM_VALUE(ValueType, type_);
}

int PrimitiveValue::CompareTo(const PrimitiveValue& other) const {
  int result = CompareUsingLessThan(type_, other.type_);
  if (result != 0) {
    return result;
  }
  switch (type_) {
    case ValueType::kNullHigh: FALLTHROUGH_INTENDED;
    case ValueType::kNullLow: FALLTHROUGH_INTENDED;
    case ValueType::kCounter: FALLTHROUGH_INTENDED;
    case ValueType::kSSForward: FALLTHROUGH_INTENDED;
    case ValueType::kSSReverse: FALLTHROUGH_INTENDED;
    case ValueType::kFalse: FALLTHROUGH_INTENDED;
    case ValueType::kTrue: FALLTHROUGH_INTENDED;
    case ValueType::kFalseDescending: FALLTHROUGH_INTENDED;
    case ValueType::kTrueDescending: FALLTHROUGH_INTENDED;
    case ValueType::kLowest: FALLTHROUGH_INTENDED;
    case ValueType::kHighest: FALLTHROUGH_INTENDED;
    case ValueType::kMaxByte:
      return 0;
    case ValueType::kCollStringDescending: FALLTHROUGH_INTENDED;
    case ValueType::kStringDescending:
      return other.str_val_.compare(str_val_);
    case ValueType::kCollString: FALLTHROUGH_INTENDED;
    case ValueType::kString:
      return str_val_.compare(other.str_val_);
    case ValueType::kInt64Descending:
      return CompareUsingLessThan(other.int64_val_, int64_val_);
    case ValueType::kInt32Descending:
      return CompareUsingLessThan(other.int32_val_, int32_val_);
    case ValueType::kInt32: FALLTHROUGH_INTENDED;
    case ValueType::kWriteId:
      return CompareUsingLessThan(int32_val_, other.int32_val_);
    case ValueType::kUInt32Descending:
      return CompareUsingLessThan(other.uint32_val_, uint32_val_);
    case ValueType::kColocationId: FALLTHROUGH_INTENDED;
    case ValueType::kSubTransactionId: FALLTHROUGH_INTENDED;
    case ValueType::kUInt32:
      return CompareUsingLessThan(uint32_val_, other.uint32_val_);
    case ValueType::kUInt64Descending:
      return CompareUsingLessThan(other.uint64_val_, uint64_val_);
    case ValueType::kUInt64:
      return CompareUsingLessThan(uint64_val_, other.uint64_val_);
    case ValueType::kInt64: FALLTHROUGH_INTENDED;
    case ValueType::kArrayIndex:
      return CompareUsingLessThan(int64_val_, other.int64_val_);
    case ValueType::kDoubleDescending:
      return CompareUsingLessThan(other.double_val_, double_val_);
    case ValueType::kDouble:
      return CompareUsingLessThan(double_val_, other.double_val_);
    case ValueType::kFloatDescending:
      return CompareUsingLessThan(other.float_val_, float_val_);
    case ValueType::kFloat:
      return CompareUsingLessThan(float_val_, other.float_val_);
    case ValueType::kDecimalDescending:
      return other.decimal_val_.compare(decimal_val_);
    case ValueType::kDecimal:
      return decimal_val_.compare(other.decimal_val_);
    case ValueType::kVarIntDescending:
      return other.varint_val_.compare(varint_val_);
    case ValueType::kVarInt:
      return varint_val_.compare(other.varint_val_);
    case ValueType::kIntentTypeSet: FALLTHROUGH_INTENDED;
    case ValueType::kObsoleteIntentTypeSet: FALLTHROUGH_INTENDED;
    case ValueType::kObsoleteIntentType: FALLTHROUGH_INTENDED;
    case ValueType::kUInt16Hash:
      return CompareUsingLessThan(uint16_val_, other.uint16_val_);
    case ValueType::kTimestampDescending:
      return CompareUsingLessThan(other.timestamp_val_, timestamp_val_);
    case ValueType::kTimestamp:
      return CompareUsingLessThan(timestamp_val_, other.timestamp_val_);
    case ValueType::kInetaddress:
      return CompareUsingLessThan(*inetaddress_val_, *(other.inetaddress_val_));
    case ValueType::kInetaddressDescending:
      return CompareUsingLessThan(*(other.inetaddress_val_), *inetaddress_val_);
    case ValueType::kFrozenDescending: FALLTHROUGH_INTENDED;
    case ValueType::kFrozen: {
      // Compare elements one by one.
      size_t min_size = std::min(frozen_val_->size(), other.frozen_val_->size());
      for (size_t i = 0; i < min_size; i++) {
        result = frozen_val_->at(i).CompareTo(other.frozen_val_->at(i));
        if (result != 0) {
          return result;
        }
      }

      // If elements are equal, compare lengths.
      if (type_ == ValueType::kFrozenDescending) {
        return CompareUsingLessThan(other.frozen_val_->size(), frozen_val_->size());
      } else {
        return CompareUsingLessThan(frozen_val_->size(), other.frozen_val_->size());
      }
    }
    case ValueType::kTransactionApplyState: FALLTHROUGH_INTENDED;
    case ValueType::kExternalTransactionId: FALLTHROUGH_INTENDED;
    case ValueType::kTransactionId: FALLTHROUGH_INTENDED;
    case ValueType::kTableId: FALLTHROUGH_INTENDED;
    case ValueType::kUuidDescending:
      return CompareUsingLessThan(other.uuid_val_, uuid_val_);
    case ValueType::kUuid:
      return CompareUsingLessThan(uuid_val_, other.uuid_val_);
    case ValueType::kColumnId: FALLTHROUGH_INTENDED;
    case ValueType::kSystemColumnId:
      return CompareUsingLessThan(column_id_val_, other.column_id_val_);
    case ValueType::kHybridTime:
      // HybridTimes are sorted in reverse order when wrapped in a PrimitiveValue.
      return -hybrid_time_val_.CompareTo(other.hybrid_time_val_);
    case ValueType::kGinNull:
      return CompareUsingLessThan(gin_null_val_, other.gin_null_val_);
    IGNORE_NON_PRIMITIVE_VALUE_TYPES_IN_SWITCH;
  }
  LOG(FATAL) << "Comparing invalid PrimitiveValues: " << *this << " and " << other;
}

PrimitiveValue::PrimitiveValue() : type_(ValueType::kNullLow) {
}

// This is used to initialize kNullLow, kNullHigh, kTrue, kFalse constants.
PrimitiveValue::PrimitiveValue(ValueType value_type)
    : type_(value_type) {
  complex_data_structure_ = nullptr;
  if (value_type == ValueType::kString || value_type == ValueType::kStringDescending) {
    new(&str_val_) std::string();
  } else if (value_type == ValueType::kInetaddress
      || value_type == ValueType::kInetaddressDescending) {
    inetaddress_val_ = new InetAddress();
  } else if (value_type == ValueType::kDecimal || value_type == ValueType::kDecimalDescending) {
    new(&decimal_val_) std::string();
  } else if (value_type == ValueType::kUuid || value_type == ValueType::kUuidDescending) {
    new(&uuid_val_) Uuid();
  } else if (value_type == ValueType::kFrozen || value_type == ValueType::kFrozenDescending) {
    frozen_val_ = new FrozenContainer();
  } else if (value_type == ValueType::kJsonb) {
    new(&json_val_) std::string();
  }
}

PrimitiveValue::PrimitiveValue(const PrimitiveValue& other) {
  if (other.IsString()) {
    type_ = other.type_;
    new(&str_val_) std::string(other.str_val_);
  } else if (other.type_ == ValueType::kJsonb) {
    type_ = other.type_;
    new(&json_val_) std::string(other.json_val_);
  } else if (other.type_ == ValueType::kInetaddress
      || other.type_ == ValueType::kInetaddressDescending) {
    type_ = other.type_;
    inetaddress_val_ = new InetAddress(*(other.inetaddress_val_));
  } else if (other.type_ == ValueType::kDecimal || other.type_ == ValueType::kDecimalDescending) {
    type_ = other.type_;
    new(&decimal_val_) std::string(other.decimal_val_);
  } else if (other.type_ == ValueType::kVarInt || other.type_ == ValueType::kVarIntDescending) {
    type_ = other.type_;
    new(&varint_val_) std::string(other.varint_val_);
  } else if (other.type_ == ValueType::kUuid || other.type_ == ValueType::kUuidDescending) {
    type_ = other.type_;
    new(&uuid_val_) Uuid(std::move((other.uuid_val_)));
  } else if (other.type_ == ValueType::kFrozen || other.type_ == ValueType::kFrozenDescending ) {
    type_ = other.type_;
    frozen_val_ = new FrozenContainer(*(other.frozen_val_));
  } else {
    memmove(static_cast<void*>(this), &other, sizeof(PrimitiveValue));
  }
  ttl_seconds_ = other.ttl_seconds_;
  write_time_ = other.write_time_;
}

PrimitiveValue::PrimitiveValue(const Slice& s, SortOrder sort_order, bool is_collate) {
  if (sort_order == SortOrder::kDescending) {
    type_ = is_collate ? ValueType::kCollStringDescending : ValueType::kStringDescending;
  } else {
    type_ = is_collate ? ValueType::kCollString : ValueType::kString;
  }
  new(&str_val_) std::string(s.cdata(), s.cend());
}

PrimitiveValue::PrimitiveValue(const std::string& s, SortOrder sort_order, bool is_collate) {
  if (sort_order == SortOrder::kDescending) {
    type_ = is_collate ? ValueType::kCollStringDescending : ValueType::kStringDescending;
  } else {
    type_ = is_collate ? ValueType::kCollString : ValueType::kString;
  }
  new(&str_val_) std::string(s);
}

PrimitiveValue::PrimitiveValue(const char* s, SortOrder sort_order, bool is_collate) {
  if (sort_order == SortOrder::kDescending) {
    type_ = is_collate ? ValueType::kCollStringDescending : ValueType::kStringDescending;
  } else {
    type_ = is_collate ? ValueType::kCollString : ValueType::kString;
  }
  new(&str_val_) std::string(s);
}

PrimitiveValue::PrimitiveValue(int64_t v, SortOrder sort_order) {
  if (sort_order == SortOrder::kDescending) {
    type_ = ValueType::kInt64Descending;
  } else {
    type_ = ValueType::kInt64;
  }
  // Avoid using an initializer for a union field (got surprising and unexpected results with
  // that approach). Use a direct assignment instead.
  int64_val_ = v;
}

PrimitiveValue::PrimitiveValue(const Timestamp& timestamp, SortOrder sort_order) {
  if (sort_order == SortOrder::kDescending) {
    type_ = ValueType::kTimestampDescending;
  } else {
    type_ = ValueType::kTimestamp;
  }
  timestamp_val_ = timestamp;
}

PrimitiveValue::PrimitiveValue(const InetAddress& inetaddress, SortOrder sort_order) {
  if (sort_order == SortOrder::kDescending) {
    type_ = ValueType::kInetaddressDescending;
  } else {
    type_ = ValueType::kInetaddress;
  }
  inetaddress_val_ = new InetAddress(inetaddress);
}

PrimitiveValue::PrimitiveValue(const Uuid& uuid, SortOrder sort_order) {
  if (sort_order == SortOrder::kDescending) {
    type_ = ValueType::kUuidDescending;
  } else {
    type_ = ValueType::kUuid;
  }
  uuid_val_ = uuid;
}

PrimitiveValue::PrimitiveValue(const HybridTime& hybrid_time) : type_(ValueType::kHybridTime) {
  hybrid_time_val_ = DocHybridTime(hybrid_time);
}

PrimitiveValue::PrimitiveValue(const DocHybridTime& hybrid_time)
    : type_(ValueType::kHybridTime),
      hybrid_time_val_(hybrid_time) {
}

PrimitiveValue::PrimitiveValue(const ColumnId column_id) : type_(ValueType::kColumnId) {
  column_id_val_ = column_id;
}

PrimitiveValue::~PrimitiveValue() {
  if (IsString()) {
    str_val_.~basic_string();
  } else if (type_ == ValueType::kJsonb) {
    json_val_.~basic_string();
  } else if (type_ == ValueType::kInetaddress || type_ == ValueType::kInetaddressDescending) {
    delete inetaddress_val_;
  } else if (type_ == ValueType::kDecimal || type_ == ValueType::kDecimalDescending) {
    decimal_val_.~basic_string();
  } else if (type_ == ValueType::kVarInt || type_ == ValueType::kVarIntDescending) {
    varint_val_.~basic_string();
  } else if (type_ == ValueType::kFrozen) {
    delete frozen_val_;
  }
  // HybridTime does not need its destructor to be called, because it is a simple wrapper over an
  // unsigned 64-bit integer.
}

PrimitiveValue PrimitiveValue::NullValue(SortingType sorting) {
  using SortingType = SortingType;

  return PrimitiveValue(
      sorting == SortingType::kAscendingNullsLast || sorting == SortingType::kDescendingNullsLast
      ? ValueType::kNullHigh
      : ValueType::kNullLow);
}

DocHybridTime PrimitiveValue::hybrid_time() const {
  DCHECK(type_ == ValueType::kHybridTime);
  return hybrid_time_val_;
}

bool PrimitiveValue::IsPrimitive() const {
  return IsPrimitiveValueType(type_);
}

bool PrimitiveValue::IsTombstoneOrPrimitive() const {
  return IsPrimitiveValueType(type_) || type_ == ValueType::kTombstone;
}

bool PrimitiveValue::IsInfinity() const {
  return type_ == ValueType::kHighest || type_ == ValueType::kLowest;
}

bool PrimitiveValue::IsInt64() const {
  return ValueType::kInt64 == type_ || ValueType::kInt64Descending == type_;
}

bool PrimitiveValue::IsString() const {
  return ValueType::kString == type_ || ValueType::kStringDescending == type_ ||
         ValueType::kCollString == type_ || ValueType::kCollStringDescending == type_;
}

bool PrimitiveValue::IsDouble() const {
  return ValueType::kDouble == type_ || ValueType::kDoubleDescending == type_;
}

int32_t PrimitiveValue::GetInt32() const {
  DCHECK(ValueType::kInt32 == type_ || ValueType::kInt32Descending == type_);
  return int32_val_;
}

uint32_t PrimitiveValue::GetUInt32() const {
  DCHECK(ValueType::kUInt32 == type_ || ValueType::kUInt32Descending == type_);
  return uint32_val_;
}

int64_t PrimitiveValue::GetInt64() const {
  DCHECK(ValueType::kInt64 == type_ || ValueType::kInt64Descending == type_);
  return int64_val_;
}

uint64_t PrimitiveValue::GetUInt64() const {
  DCHECK(ValueType::kUInt64 == type_ || ValueType::kUInt64Descending == type_);
  return uint64_val_;
}

uint16_t PrimitiveValue::GetUInt16() const {
  DCHECK(ValueType::kUInt16Hash == type_ ||
         ValueType::kObsoleteIntentTypeSet == type_ ||
         ValueType::kObsoleteIntentType == type_ ||
         ValueType::kIntentTypeSet == type_);
  return uint16_val_;
}

float PrimitiveValue::GetFloat() const {
  DCHECK(ValueType::kFloat == type_ || ValueType::kFloatDescending == type_);
  return float_val_;
}

const std::string& PrimitiveValue::GetDecimal() const {
  DCHECK(ValueType::kDecimal == type_ || ValueType::kDecimalDescending == type_);
  return decimal_val_;
}

const std::string& PrimitiveValue::GetVarInt() const {
  DCHECK(ValueType::kVarInt == type_ || ValueType::kVarIntDescending == type_);
  return varint_val_;
}

Timestamp PrimitiveValue::GetTimestamp() const {
  DCHECK(ValueType::kTimestamp == type_ || ValueType::kTimestampDescending == type_);
  return timestamp_val_;
}

const InetAddress* PrimitiveValue::GetInetaddress() const {
  DCHECK(type_ == ValueType::kInetaddress || type_ == ValueType::kInetaddressDescending);
  return inetaddress_val_;
}

const std::string& PrimitiveValue::GetJson() const {
  DCHECK(type_ == ValueType::kJsonb);
  return json_val_;
}

const Uuid& PrimitiveValue::GetUuid() const {
  DCHECK(type_ == ValueType::kUuid || type_ == ValueType::kUuidDescending ||
         type_ == ValueType::kTransactionId || type_ == ValueType::kTableId);
  return uuid_val_;
}

ColumnId PrimitiveValue::GetColumnId() const {
  DCHECK(type_ == ValueType::kColumnId || type_ == ValueType::kSystemColumnId);
  return column_id_val_;
}

uint8_t PrimitiveValue::GetGinNull() const {
  DCHECK(ValueType::kGinNull == type_);
  return gin_null_val_;
}

void PrimitiveValue::MoveFrom(PrimitiveValue* other) {
  if (this == other) {
    return;
  }

  ttl_seconds_ = other->ttl_seconds_;
  write_time_ = other->write_time_;
  if (other->IsString()) {
    type_ = other->type_;
    new(&str_val_) std::string(std::move(other->str_val_));
    // The moved-from object should now be in a "valid but unspecified" state as per the standard.
  } else if (other->type_ == ValueType::kInetaddress
      || other->type_ == ValueType::kInetaddressDescending) {
    type_ = other->type_;
    inetaddress_val_ = new InetAddress(std::move(*(other->inetaddress_val_)));
  } else if (other->type_ == ValueType::kJsonb) {
    type_ = other->type_;
    new(&json_val_) std::string(std::move(other->json_val_));
  } else if (other->type_ == ValueType::kDecimal ||
      other->type_ == ValueType::kDecimalDescending) {
    type_ = other->type_;
    new(&decimal_val_) std::string(std::move(other->decimal_val_));
  } else if (other->type_ == ValueType::kVarInt ||
      other->type_ == ValueType::kVarIntDescending) {
    type_ = other->type_;
    new(&varint_val_) std::string(std::move(other->varint_val_));
  } else if (other->type_ == ValueType::kUuid || other->type_ == ValueType::kUuidDescending) {
    type_ = other->type_;
    new(&uuid_val_) Uuid(std::move((other->uuid_val_)));
  } else if (other->type_ == ValueType::kFrozen) {
    type_ = other->type_;
    frozen_val_ = new FrozenContainer(std::move(*(other->frozen_val_)));
  } else {
    // Non-string primitive values only have plain old data. We are assuming there is no overlap
    // between the two objects, so we're using memcpy instead of memmove.
    memcpy(static_cast<void*>(this), other, sizeof(PrimitiveValue));
#ifndef NDEBUG
    // We could just leave the old object as is for it to be in a "valid but unspecified" state.
    // However, in debug mode we clear the old object's state to make sure we don't attempt to use
    // it.
    memset(static_cast<void*>(other), 0xab, sizeof(PrimitiveValue));
    // Restore the type. There should be no deallocation for non-string types anyway.
    other->type_ = ValueType::kNullLow;
#endif
  }
}

SortOrder SortOrderFromColumnSchemaSortingType(SortingType sorting_type) {
  if (sorting_type == SortingType::kDescending ||
      sorting_type == SortingType::kDescendingNullsLast) {
    return SortOrder::kDescending;
  }
  return SortOrder::kAscending;
}

PrimitiveValue PrimitiveValue::FromQLValuePB(
    const LWQLValuePB& value, SortingType sorting_type, bool check_is_collate) {
  return DoFromQLValuePB(value, sorting_type, check_is_collate);
}

PrimitiveValue PrimitiveValue::FromQLValuePB(
    const QLValuePB& value, SortingType sorting_type, bool check_is_collate) {
  return DoFromQLValuePB(value, sorting_type, check_is_collate);
}

template <class PB>
PrimitiveValue PrimitiveValue::DoFromQLValuePB(
    const PB& value, SortingType sorting_type, bool check_is_collate) {
  const auto sort_order = SortOrderFromColumnSchemaSortingType(sorting_type);

  switch (value.value_case()) {
    case QLValuePB::kInt8Value:
      return PrimitiveValue::Int32(value.int8_value(), sort_order);
    case QLValuePB::kInt16Value:
      return PrimitiveValue::Int32(value.int16_value(), sort_order);
    case QLValuePB::kInt32Value:
      return PrimitiveValue::Int32(value.int32_value(), sort_order);
    case QLValuePB::kInt64Value:
      return PrimitiveValue(value.int64_value(), sort_order);
    case QLValuePB::kUint32Value:
      return PrimitiveValue::UInt32(value.uint32_value(), sort_order);
    case QLValuePB::kUint64Value:
      return PrimitiveValue::UInt64(value.uint64_value(), sort_order);
    case QLValuePB::kFloatValue: {
      float f = value.float_value();
      return PrimitiveValue::Float(util::CanonicalizeFloat(f), sort_order);
    }
    case QLValuePB::kDoubleValue: {
      double d = value.double_value();
      return PrimitiveValue::Double(util::CanonicalizeDouble(d), sort_order);
    }
    case QLValuePB::kDecimalValue:
      return PrimitiveValue::Decimal(value.decimal_value(), sort_order);
    case QLValuePB::kVarintValue:
      return PrimitiveValue::VarInt(value.varint_value(), sort_order);
    case QLValuePB::kStringValue: {
      const auto& val = value.string_value();
      // In both Postgres and YCQL, character value cannot have embedded \0 byte.
      // Redis allows embedded \0 byte but it does not use QLValuePB so will not
      // come here to pick up 'is_collate'. Therefore, if the value is not empty
      // and the first byte is \0, it indicates this is a collation encoded string.
      if (!val.empty() && val[0] == '\0' && sorting_type != SortingType::kNotSpecified) {
        // An empty collation encoded string is at least 3 bytes.
        CHECK_GE(val.size(), 3);
        return PrimitiveValue(val, sort_order, true /* is_collate */);
      }
      return PrimitiveValue(val, sort_order);
    }
    case QLValuePB::kBinaryValue:
      // TODO consider using dedicated encoding for binary (not string) to avoid overhead of
      // zero-encoding for keys (since zero-bytes could be common for binary)
      return PrimitiveValue(value.binary_value(), sort_order);
    case QLValuePB::kBoolValue:
      return PrimitiveValue(sort_order == SortOrder::kDescending
                            ? (value.bool_value() ? ValueType::kTrueDescending
                                                  : ValueType::kFalseDescending)
                            : (value.bool_value() ? ValueType::kTrue
                                                  : ValueType::kFalse));
    case QLValuePB::kTimestampValue:
      return PrimitiveValue(QLValue::timestamp_value(value), sort_order);
    case QLValuePB::kDateValue:
      return PrimitiveValue::UInt32(value.date_value(), sort_order);
    case QLValuePB::kTimeValue:
      return PrimitiveValue(value.time_value(), sort_order);
    case QLValuePB::kInetaddressValue:
      return PrimitiveValue(QLValue::inetaddress_value(value), sort_order);
    case QLValuePB::kJsonbValue:
      return PrimitiveValue::Jsonb(QLValue::jsonb_value(value));
    case QLValuePB::kUuidValue:
      return PrimitiveValue(QLValue::uuid_value(value), sort_order);
    case QLValuePB::kTimeuuidValue:
      return PrimitiveValue(QLValue::timeuuid_value(value), sort_order);
    case QLValuePB::kFrozenValue: {
      const auto& frozen = value.frozen_value();
      PrimitiveValue pv(ValueType::kFrozen);
      auto null_value_type = ValueType::kNullLow;
      if (sort_order == SortOrder::kDescending) {
        null_value_type = ValueType::kNullHigh;
        pv.type_ = ValueType::kFrozenDescending;
      }

      for (const auto& elem : frozen.elems()) {
        if (IsNull(elem)) {
          pv.frozen_val_->emplace_back(null_value_type);
        } else {
          pv.frozen_val_->push_back(PrimitiveValue::FromQLValuePB(elem, sorting_type));
        }
      }
      return pv;
    }
    case QLValuePB::VALUE_NOT_SET:
      return PrimitiveValue::kTombstone;

    case QLValuePB::kMapValue: FALLTHROUGH_INTENDED;
    case QLValuePB::kSetValue: FALLTHROUGH_INTENDED;
    case QLValuePB::kListValue:
      break;

    case QLValuePB::kVirtualValue:
      return PrimitiveValue(VirtualValueToValueType(value.virtual_value()));
    case QLValuePB::kGinNullValue:
      return PrimitiveValue::GinNull(value.gin_null_value());

    // default: fall through
  }

  LOG(FATAL) << "Unsupported datatype in PrimitiveValue: " << value.value_case();
}

void PrimitiveValue::ToQLValuePB(const PrimitiveValue& primitive_value,
                                 const std::shared_ptr<QLType>& ql_type,
                                 QLValuePB* ql_value) {
  // DocDB sets type to kInvalidValueType for SubDocuments that don't exist. That's why they need
  // to be set to Null in QLValue.
  if (primitive_value.value_type() == ValueType::kNullLow ||
      primitive_value.value_type() == ValueType::kNullHigh ||
      primitive_value.value_type() == ValueType::kInvalid ||
      primitive_value.value_type() == ValueType::kTombstone) {
    SetNull(ql_value);
    return;
  }

  // For ybgin indexes, null category can be set on any index key column, regardless of the column's
  // actual type.  The column's actual type cannot be kGinNull, so it throws error in the below
  // switch.
  if (primitive_value.value_type() == ValueType::kGinNull) {
    ql_value->set_gin_null_value(primitive_value.GetGinNull());
    return;
  }

  switch (ql_type->main()) {
    case INT8:
      ql_value->set_int8_value(static_cast<int8_t>(primitive_value.GetInt32()));
      return;
    case INT16:
      ql_value->set_int16_value(static_cast<int16_t>(primitive_value.GetInt32()));
      return;
    case INT32:
      ql_value->set_int32_value(primitive_value.GetInt32());
      return;
    case INT64:
      ql_value->set_int64_value(primitive_value.GetInt64());
      return;
    case UINT32:
      ql_value->set_uint32_value(primitive_value.GetUInt32());
      return;
    case UINT64:
      ql_value->set_uint64_value(primitive_value.GetUInt64());
      return;
    case FLOAT:
      ql_value->set_float_value(primitive_value.GetFloat());
      return;
    case DOUBLE:
      ql_value->set_double_value(primitive_value.GetDouble());
      return;
    case DECIMAL:
      ql_value->set_decimal_value(primitive_value.GetDecimal());
      return;
    case VARINT:
      ql_value->set_varint_value(primitive_value.GetVarInt());
      return;
    case BOOL:
      ql_value->set_bool_value(primitive_value.value_type() == ValueType::kTrue ||
                               primitive_value.value_type() == ValueType::kTrueDescending);
      return;
    case TIMESTAMP:
      ql_value->set_timestamp_value(primitive_value.GetTimestamp().ToInt64());
      return;
    case DATE:
      ql_value->set_date_value(primitive_value.GetUInt32());
      return;
    case TIME:
      ql_value->set_time_value(primitive_value.GetInt64());
      return;
    case INET: {
      QLValue temp_value;
      temp_value.set_inetaddress_value(*primitive_value.GetInetaddress());
      *ql_value = std::move(*temp_value.mutable_value());
      return;
    }
    case JSONB: {
      QLValue temp_value;
      temp_value.set_jsonb_value(primitive_value.GetJson());
      *ql_value = std::move(*temp_value.mutable_value());
      return;
    }
    case UUID: {
      QLValue temp_value;
      temp_value.set_uuid_value(primitive_value.GetUuid());
      *ql_value = std::move(*temp_value.mutable_value());
      return;
    }
    case TIMEUUID: {
      QLValue temp_value;
      temp_value.set_timeuuid_value(primitive_value.GetUuid());
      *ql_value = std::move(*temp_value.mutable_value());
      return;
    }
    case STRING:
      ql_value->set_string_value(primitive_value.GetString());
      return;
    case BINARY:
      ql_value->set_binary_value(primitive_value.GetString());
      return;
    case FROZEN: {
      const auto& type = ql_type->param_type(0);
      QLSeqValuePB *frozen_value = ql_value->mutable_frozen_value();
      frozen_value->clear_elems();
      switch (type->main()) {
        case MAP: {
          const std::shared_ptr<QLType>& keys_type = type->param_type(0);
          const std::shared_ptr<QLType>& values_type = type->param_type(1);
          for (size_t i = 0; i < primitive_value.frozen_val_->size(); i++) {
            if (i % 2 == 0) {
              QLValuePB *key = frozen_value->add_elems();
              PrimitiveValue::ToQLValuePB((*primitive_value.frozen_val_)[i], keys_type, key);
            } else {
              QLValuePB *value = frozen_value->add_elems();
              PrimitiveValue::ToQLValuePB((*primitive_value.frozen_val_)[i], values_type, value);
            }
          }
          return;
        }
        case SET: FALLTHROUGH_INTENDED;
        case LIST: {
          const std::shared_ptr<QLType>& elems_type = type->param_type(0);
          for (const auto &pv : *primitive_value.frozen_val_) {
            QLValuePB *elem = frozen_value->add_elems();
            PrimitiveValue::ToQLValuePB(pv, elems_type, elem);
          }
          return;
        }
        case USER_DEFINED_TYPE: {
          for (size_t i = 0; i < primitive_value.frozen_val_->size(); i++) {
            QLValuePB *value = frozen_value->add_elems();
            PrimitiveValue::ToQLValuePB(
                (*primitive_value.frozen_val_)[i], type->param_type(i), value);
          }
          return;
        }

        default:
          break;
      }
      FATAL_INVALID_ENUM_VALUE(DataType, type->main());
    }

    case NULL_VALUE_TYPE: FALLTHROUGH_INTENDED;
    case MAP: FALLTHROUGH_INTENDED;
    case SET: FALLTHROUGH_INTENDED;
    case LIST: FALLTHROUGH_INTENDED;
    case TUPLE: FALLTHROUGH_INTENDED;
    case TYPEARGS: FALLTHROUGH_INTENDED;
    case USER_DEFINED_TYPE: FALLTHROUGH_INTENDED;

    case UINT8:  FALLTHROUGH_INTENDED;
    case UINT16: FALLTHROUGH_INTENDED;
    case UNKNOWN_DATA: FALLTHROUGH_INTENDED;
    case GIN_NULL:
      break;

    // default: fall through
  }

  LOG(FATAL) << "Unsupported datatype " << ql_type->ToString();
}

}  // namespace docdb
}  // namespace yb
