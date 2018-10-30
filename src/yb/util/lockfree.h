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

#ifndef YB_UTIL_LOCKFREE_H
#define YB_UTIL_LOCKFREE_H

#include <atomic>

namespace yb {

// Multi producer - singe consumer queue.
template <class T>
class MPSCQueue {
 public:
  // Thread safe - could be invoked from multiple threads.
  void Push(T* value) {
    T* old_head = push_head_.load(std::memory_order_acquire);
    for (;;) {
      SetNext(value, old_head);
      if (push_head_.compare_exchange_weak(old_head, value, std::memory_order_acq_rel)) {
        break;
      }
    }
  }

  // Could be invoked only by one thread at time.
  T* Pop() {
    if (!pop_head_) {
      PreparePop();
    }
    auto result = pop_head_;
    if (!result) {
      return nullptr;
    }
    pop_head_ = GetNext(result);
    return result;
  }

 private:
  void PreparePop() {
    T* current = push_head_.exchange(nullptr, std::memory_order_acq_rel);
    // Reverse original list.
    T* prev = nullptr;
    while (current) {
      auto next = GetNext(current);
      SetNext(current, prev);
      prev = current;
      current = next;
    }
    pop_head_ = prev;
  }

  // List of entries ready for pop, pop head points to the entry that should be returned first.
  T* pop_head_ = nullptr;
  // List of push entries, push head points to last pushed entry.
  std::atomic<T*> push_head_{nullptr};
};

template <class T>
class MPSCQueueEntry {
 public:
  void SetNext(T* next) {
    next_ = next;
  }

  T* GetNext() const {
    return next_;
  }

 private:
  T* next_ = nullptr;
};

template <class T>
void SetNext(MPSCQueueEntry<T>* entry, T* next) {
  entry->SetNext(next);
}

template <class T>
T* GetNext(const MPSCQueueEntry<T>* entry) {
  return entry->GetNext();
}

} // namespace yb

#endif // YB_UTIL_LOCKFREE_H
