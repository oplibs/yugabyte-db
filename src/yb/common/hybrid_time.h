// Licensed to the Apache Software Foundation (ASF) under one
// or more contributor license agreements.  See the NOTICE file
// distributed with this work for additional information
// regarding copyright ownership.  The ASF licenses this file
// to you under the Apache License, Version 2.0 (the
// "License"); you may not use this file except in compliance
// with the License.  You may obtain a copy of the License at
//
//   http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing,
// software distributed under the License is distributed on an
// "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
// KIND, either express or implied.  See the License for the
// specific language governing permissions and limitations
// under the License.
//
// The following only applies to changes made to this file as part of YugaByte development.
//
// Portions Copyright (c) YugaByte, Inc.
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

// Portions Copyright (c) YugaByte, Inc.

#pragma once

#include <inttypes.h>

#include <limits>
#include <string>

#include "yb/util/status_fwd.h"
#include "yb/util/faststring.h"
#include "yb/util/monotime.h"
#include "yb/util/physical_time.h"

namespace yb {

class Slice;

// An alias for the raw in-memory representation of a HybridTime.
using HybridTimeRepr = uint64_t;

// An alias for the in-memory representation of the logical component of a HybridTime.
using LogicalTimeComponent = uint32_t;

constexpr HybridTimeRepr kMinHybridTimeValue = std::numeric_limits<HybridTimeRepr>::min();
constexpr HybridTimeRepr kMaxHybridTimeValue = std::numeric_limits<HybridTimeRepr>::max();
constexpr HybridTimeRepr kInitialHybridTimeValue = kMinHybridTimeValue + 1;
constexpr HybridTimeRepr kInvalidHybridTimeValue = kMaxHybridTimeValue - 1;

// A transaction hybrid time generated by a Clock.
class HybridTime {
 public:
  // TODO: replace all mentions of this with HybridTimeRepr itself and deprecate val_type.
  using val_type = HybridTimeRepr;

  // Left-shifting the microseconds timestamp 12 bits gives us 12 bits for the logical value and
  // should still keep accurate microseconds time until 2100+.
  static constexpr int kBitsForLogicalComponent = 12;

  // This mask gives us back the logical bits.
  static constexpr HybridTimeRepr kLogicalBitMask = (1 << kBitsForLogicalComponent) - 1;

  // An initial transaction hybrid time, higher than min so that we can have
  // a hybrid time guaranteed to be lower than all generated hybrid times.
  static const HybridTime kInitial;

  // An invalid transaction hybrid time -- HybridTime types initialize to this variable.
  static const HybridTime kInvalid;

  // The maximum hybrid time.
  static const HybridTime kMax;

  // The minimum hybrid time.
  static const HybridTime kMin;

  // Hybrid times are converted to debug strings as <this_string_constant>(<hybrid_time_value>).
  static const char* const kHybridTimeDebugStrPrefix;

  static const size_t SizeOfHybridTimeRepr = sizeof(HybridTimeRepr);

  // ----------------------------------------------------------------------------------------------
  // Constructors / static factories

  HybridTime() noexcept : v(kInvalidHybridTimeValue) {}

  HybridTime(MicrosTime micros, LogicalTimeComponent logical_value) {
    v = (micros << kBitsForLogicalComponent) + logical_value;
  }

  static inline HybridTime FromMicrosecondsAndLogicalValue(
      MicrosTime micros, LogicalTimeComponent logical_value) {
    return HybridTime(micros, logical_value);
  }

  static inline HybridTime FromMicros(MicrosTime micros) {
    return HybridTime(micros, 0);
  }

  explicit HybridTime(uint64_t val) : v(val) {}

  bool operator ==(const HybridTime &other) const {
    return v == other.v;
  }
  bool operator !=(const HybridTime &other) const {
    return v != other.v;
  }

  // Decode a hybrid time from the given input slice.
  // Mutates the slice to point after the decoded hybrid time.
  // Returns true upon success.
  bool DecodeFrom(Slice *input);

  // Append the hybrid time to the given buffer.
  void AppendAsUint64To(faststring *dst) const;
  void AppendAsUint64To(std::string* dst) const;

  int CompareTo(const HybridTime &other) const;

  std::string ToString() const;

  std::string ToDebugString() const;

  // Returns this hybrid time as an uint64_t
  uint64_t ToUint64() const;

  // Return the highest value of a HybridTime that is lower than this. If called on kMin, returns
  // kMin itself, because it cannot be decremented.
  HybridTime Decremented() const {
    if (v == 0) return *this;
    return HybridTime(v - 1);
  }

  // Returns the hybrid time value by the smallest possible amount. For invalid / max hybrid time,
  // returns the unmodified value.
  HybridTime Incremented() const {
    if (v >= kInvalidHybridTimeValue) return *this;
    return HybridTime(v + 1);
  }

  HybridTime AddMicroseconds(MicrosTime micros) const {
    if (is_special()) return *this;
    return HybridTime(v + (micros << kBitsForLogicalComponent));
  }

  HybridTime AddMilliseconds(int64_t millis) const {
    return AddMicroseconds(millis * MonoTime::kMicrosecondsPerMillisecond);
  }

  HybridTime AddSeconds(int64_t seconds) const {
    return AddMicroseconds(seconds * MonoTime::kMicrosecondsPerSecond);
  }

  HybridTime AddDelta(MonoDelta delta) const {
    return AddMicroseconds(delta.ToMicroseconds());
  }

  // Sets this hybrid time from 'value'
  Status FromUint64(uint64_t value);

  static HybridTime FromPB(uint64_t value) {
    return value ? HybridTime(value) : HybridTime();
  }

  HybridTimeRepr value() const { return v; }

  // Returns this HybridTime if valid, otherwise returns the one provided.
  HybridTime GetValueOr(const HybridTime& other) const {
    return is_valid() ? *this : other;
  }

  bool is_special() const {
    switch (v) {
      case kMinHybridTimeValue: FALLTHROUGH_INTENDED;
      case kMaxHybridTimeValue: FALLTHROUGH_INTENDED;
      case kInvalidHybridTimeValue:
        return true;
      default:
        return false;
    }
  }

  bool operator <(const HybridTime& other) const {
    return CompareTo(other) < 0;
  }

  bool operator >(const HybridTime& other) const {
    return CompareTo(other) > 0;
  }

  bool operator <=(const HybridTime& other) const {
    return CompareTo(other) <= 0;
  }

  bool operator >=(const HybridTime& other) const {
    return CompareTo(other) >= 0;
  }

  // Returns the physical value embedded in this HybridTime, in microseconds.
  inline MicrosTime GetPhysicalValueMicros() const {
    return v >> kBitsForLogicalComponent;
  }

  uint64_t GetPhysicalValueMillis() const;
  uint64_t GetPhysicalValueNanos() const;

  MicrosTime CeilPhysicalValueMicros() const;

  inline int64_t PhysicalDiff(const HybridTime& other) const {
    return static_cast<int64_t>(GetPhysicalValueMicros() - other.GetPhysicalValueMicros());
  }

  inline LogicalTimeComponent GetLogicalValue() const {
    return v & kLogicalBitMask;
  }

  inline bool is_valid() const { return v != kInvalidHybridTimeValue; }

  explicit operator bool() const { return is_valid(); }

  void MakeAtLeast(HybridTime rhs) {
    if (rhs.is_valid()) {
      v = is_valid() ? std::max(v, rhs.v) : rhs.v;
    }
  }

  void MakeAtMost(HybridTime rhs) {
    if (rhs.is_valid()) {
      v = is_valid() ? std::min(v, rhs.v) : rhs.v;
    }
  }

  // Set mode for HybridTime::ToString, in case of true hybrid time is rendered as human readable.
  // It is slower than default one.
  static void TEST_SetPrettyToString(bool flag);

  // Acceptable system time formats:
  //  1. HybridTime Timestamp (in Microseconds)
  //  2. Interval
  //  3. Human readable string
  static Result<HybridTime> ParseHybridTime(std::string input);

 private:

  HybridTimeRepr v;
};

// The maximum microsecond value possible so that the corresponding HybridTime can still fit into a
// uint64_t.
constexpr MicrosTime kMaxHybridTimePhysicalMicros{
    kMaxHybridTimeValue >> HybridTime::kBitsForLogicalComponent};

class faststring;

class Slice;

inline int HybridTime::CompareTo(const HybridTime &other) const {
  if (v < other.v) {
    return -1;
  } else if (v > other.v) {
    return 1;
  }
  return 0;
}

// Given two hybrid times, determines whether the delta between end and begin them is higher,
// lower or equal to the given delta and returns 1, -1 and 0 respectively. Note that if end <
// begin we return -1.
int CompareHybridTimesToDelta(HybridTime begin, HybridTime end, MonoDelta delta);

inline std::ostream &operator <<(std::ostream &o, const HybridTime &hybridTime) {
  return o << hybridTime.ToString();
}

namespace hybrid_time_literals {

inline HybridTime operator "" _usec_ht(unsigned long long microseconds) { // NOLINT
  return HybridTime::FromMicros(microseconds);
}

} // namespace hybrid_time_literals

using hybrid_time_literals::operator"" _usec_ht;

}  // namespace yb
