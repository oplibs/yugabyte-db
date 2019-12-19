//
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

#ifndef YB_RPC_RPC_UTIL_H
#define YB_RPC_RPC_UTIL_H

#include <memory>

#include "yb/util/slice.h"

namespace yb {
namespace rpc {

// Returns slice pointing to global buffer for reading data which we want to ignore, for example in
// case of hitting memory limit.
Slice GetGlobalSkipBuffer();

}  // namespace rpc
}  // namespace yb

#endif  // YB_RPC_RPC_UTIL_H
