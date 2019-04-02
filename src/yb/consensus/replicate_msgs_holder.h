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

#ifndef YB_CONSENSUS_REPLICATE_MSGS_HOLDER_H
#define YB_CONSENSUS_REPLICATE_MSGS_HOLDER_H

#include <google/protobuf/repeated_field.h>

#include "yb/consensus/consensus_fwd.h"

namespace yb {
namespace consensus {

class ReplicateMsgsHolder {
 public:
  ReplicateMsgsHolder() : ops_(nullptr) {}

  explicit ReplicateMsgsHolder(
      google::protobuf::RepeatedPtrField<ReplicateMsg>* ops, ReplicateMsgs messages);

  ReplicateMsgsHolder(ReplicateMsgsHolder&& rhs);
  void operator=(ReplicateMsgsHolder&& rhs);

  ReplicateMsgsHolder(const ReplicateMsgsHolder&) = delete;

  void operator=(const ReplicateMsgsHolder&) = delete;

  ~ReplicateMsgsHolder();

  void Reset();

  void ReleaseOps() {
    ops_ = nullptr;
  }

 private:
  google::protobuf::RepeatedPtrField<ReplicateMsg>* ops_;

  // Reference-counted pointers to any ReplicateMsgs which are in-flight to the peer. We may have
  // loaded these messages from the LogCache, in which case we are potentially sharing the same
  // object as other peers. Since the PB request_ itself can't hold reference counts, this holds
  // them.
  ReplicateMsgs messages_;
};

}  // namespace consensus
}  // namespace yb

#endif // YB_CONSENSUS_REPLICATE_MSGS_HOLDER_H
