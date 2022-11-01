//  Copyright (c) 2011-present, Facebook, Inc.  All rights reserved.
//  This source code is licensed under the BSD-style license found in the
//  LICENSE file in the root directory of this source tree. An additional grant
//  of patent rights can be found in the PATENTS file in the same directory.
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

#pragma once

#include "yb/util/stats/iostats_context.h"
#include "yb/util/stats/perf_step_timer.h"

#ifndef IOS_CROSS_COMPILE

// increment a specific counter by the specified value
#define IOSTATS_ADD(metric, value)             \
  (yb::iostats_context.metric += value)

// Increase metric value only when it is positive
#define IOSTATS_ADD_IF_POSITIVE(metric, value)   \
  if (value > 0) { IOSTATS_ADD(metric, value); }

// reset a specific counter to zero
#define IOSTATS_RESET(metric)                  \
  (yb::iostats_context.metric = 0)

// reset all counters to zero
#define IOSTATS_RESET_ALL(thread_pool_id)      \
  (yb::iostats_context.Reset(thread_pool_id))

#define IOSTATS_SET_THREAD_POOL_ID(value)      \
  (yb::iostats_context.thread_pool_id = value)

#define IOSTATS_THREAD_POOL_ID()               \
  (yb::iostats_context.thread_pool_id)

#define IOSTATS(metric)                        \
  (yb::iostats_context.metric)

// Declare and set start time of the timer
#define IOSTATS_TIMER_GUARD(metric)                                                \
  yb::PerfStepTimer iostats_step_timer_ ## metric(&(yb::iostats_context.metric));  \
  iostats_step_timer_ ## metric.Start();

#else  // IOS_CROSS_COMPILE

#define IOSTATS_ADD(metric, value)
#define IOSTATS_ADD_IF_POSITIVE(metric, value)
#define IOSTATS_RESET(metric)
#define IOSTATS_RESET_ALL()
#define IOSTATS_SET_THREAD_POOL_ID(value)
#define IOSTATS_THREAD_POOL_ID()
#define IOSTATS(metric) 0

#define IOSTATS_TIMER_GUARD(metric)

#endif  // IOS_CROSS_COMPILE

