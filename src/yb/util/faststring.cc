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

#include "yb/util/faststring.h"

#include <memory>

#include "yb/util/logging.h"

namespace yb {

void faststring::GrowByAtLeast(size_t count) {
  // Not enough space, need to reserve more.
  // Don't reserve exactly enough space for the new string -- that makes it
  // too easy to write perf bugs where you get O(n^2) append.
  // Instead, alwayhs expand by at least 50%.

  size_t to_reserve = len_ + count;
  if (len_ + count < len_ * 3 / 2) {
    to_reserve = len_ *  3 / 2;
  }
  GrowArray(to_reserve);
}

void faststring::GrowArray(size_t newcapacity) {
  DCHECK_GE(newcapacity, capacity_);
  std::unique_ptr<uint8_t[]> newdata(new uint8_t[newcapacity]);
  if (len_ > 0) {
    memcpy(&newdata[0], &data_[0], len_);
  }
  capacity_ = newcapacity;
  if (data_ != initial_data_) {
    delete[] data_;
  } else {
    ASAN_POISON_MEMORY_REGION(initial_data_, arraysize(initial_data_));
  }

  data_ = newdata.release();
  ASAN_POISON_MEMORY_REGION(data_ + len_, capacity_ - len_);
}


} // namespace yb
