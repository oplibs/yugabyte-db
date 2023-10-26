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

#pragma once

#include "yb/util/thread_annotations_util.h"

namespace yb {
namespace rpc {

#if defined(__clang__) && __clang_major__ >= 13

struct ReactorThreadRole {
  static constexpr ThreadRole kReactor{};
  static ThreadRole Alias() RETURN_CAPABILITY(kReactor);
};

using ReactorThreadRoleGuard = CapabilityGuard<ReactorThreadRole>;

#define ON_REACTOR_THREAD REQUIRES(::yb::rpc::ReactorThreadRole::kReactor)
#define EXCLUDES_REACTOR_THREAD EXCLUDES(::yb::rpc::ReactorThreadRole::kReactor)
#define GUARDED_BY_REACTOR_THREAD GUARDED_BY(::yb::rpc::ReactorThreadRole::kReactor)

#else

struct [[maybe_unused]] ReactorThreadRoleGuard {  // NOLINT
};

#define ON_REACTOR_THREAD
#define EXCLUDES_REACTOR_THREAD
#define GUARDED_BY_REACTOR_THREAD

#endif

} // namespace rpc
} // namespace yb
