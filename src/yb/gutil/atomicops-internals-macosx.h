// Copyright 2006 Google Inc.
// All Rights Reserved.
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
//
// Implementation of atomic operations for Mac OS X.  This file should not
// be included directly.  Clients should instead include
// "base/atomicops.h".

#pragma once

typedef int32_t Atomic32;
typedef int64_t Atomic64;

// MacOS uses long for intptr_t, AtomicWord and Atomic32 are always different
// on the Mac, even when they are the same size.  Similarly, on __ppc64__,
// AtomicWord and Atomic64 are always different.  Thus, we need explicit
// casting.
#ifdef __LP64__
#define AtomicWordCastType base::subtle::Atomic64
#else
#define AtomicWordCastType Atomic32
#endif

#if defined(__LP64__) || defined(__i386__)
#define BASE_HAS_ATOMIC64 1  // Use only in tests and base/atomic*
#endif

#include <libkern/OSAtomic.h>

#if !defined(__LP64__) && defined(__ppc__)

// The Mac 64-bit OSAtomic implementations are not available for 32-bit PowerPC,
// while the underlying assembly instructions are available only some
// implementations of PowerPC.

// The following inline functions will fail with the error message at compile
// time ONLY IF they are called.  So it is safe to use this header if user
// code only calls AtomicWord and Atomic32 operations.
//
// NOTE(user): Implementation notes to implement the atomic ops below may
// be found in "PowerPC Virtual Environment Architecture, Book II,
// Version 2.02", January 28, 2005, Appendix B, page 46.  Unfortunately,
// extra care must be taken to ensure data are properly 8-byte aligned, and
// that data are returned correctly according to Mac OS X ABI specs.

inline int64_t OSAtomicCompareAndSwap64(
    int64_t oldValue, int64_t newValue, int64_t *theValue) {
  __asm__ __volatile__(
      "_OSAtomicCompareAndSwap64_not_supported_for_32_bit_ppc\n\t");
  return 0;
}

inline int64_t OSAtomicAdd64(int64_t theAmount, int64_t *theValue) {
  __asm__ __volatile__(
      "_OSAtomicAdd64_not_supported_for_32_bit_ppc\n\t");
  return 0;
}

inline int64_t OSAtomicCompareAndSwap64Barrier(
    int64_t oldValue, int64_t newValue, int64_t *theValue) {
  int64_t prev = OSAtomicCompareAndSwap64(oldValue, newValue, theValue);
  OSMemoryBarrier();
  return prev;
}

inline int64_t OSAtomicAdd64Barrier(
    int64_t theAmount, int64_t *theValue) {
  int64_t new_val = OSAtomicAdd64(theAmount, theValue);
  OSMemoryBarrier();
  return new_val;
}
#endif


namespace base {
namespace subtle {

typedef int32_t Atomic32;
typedef int64_t Atomic64;

inline void MemoryBarrier() {
  OSMemoryBarrier();
}

// 32-bit Versions.

inline Atomic32 NoBarrier_CompareAndSwap(volatile Atomic32 *ptr,
                                         Atomic32 old_value,
                                         Atomic32 new_value) {
  Atomic32 prev_value;
  do {
    if (OSAtomicCompareAndSwap32(old_value, new_value,
                                 const_cast<Atomic32*>(ptr))) {
      return old_value;
    }
    prev_value = *ptr;
  } while (prev_value == old_value);
  return prev_value;
}

inline Atomic32 NoBarrier_AtomicExchange(volatile Atomic32 *ptr,
                                         Atomic32 new_value) {
  Atomic32 old_value;
  do {
    old_value = *ptr;
  } while (!OSAtomicCompareAndSwap32(old_value, new_value,
                                     const_cast<Atomic32*>(ptr)));
  return old_value;
}

inline Atomic32 Acquire_AtomicExchange(volatile Atomic32 *ptr,
                                       Atomic32 new_value) {
  Atomic32 old_value;
  do {
    old_value = *ptr;
  } while (!OSAtomicCompareAndSwap32Barrier(old_value, new_value,
                                            const_cast<Atomic32*>(ptr)));
  return old_value;
}

inline Atomic32 Release_AtomicExchange(volatile Atomic32 *ptr,
                                       Atomic32 new_value) {
  return Acquire_AtomicExchange(ptr, new_value);
}

inline Atomic32 NoBarrier_AtomicIncrement(volatile Atomic32 *ptr,
                                          Atomic32 increment) {
  return OSAtomicAdd32(increment, const_cast<Atomic32*>(ptr));
}

inline Atomic32 Barrier_AtomicIncrement(volatile Atomic32 *ptr,
                                          Atomic32 increment) {
  return OSAtomicAdd32Barrier(increment, const_cast<Atomic32*>(ptr));
}

inline Atomic32 Acquire_CompareAndSwap(volatile Atomic32 *ptr,
                                       Atomic32 old_value,
                                       Atomic32 new_value) {
  Atomic32 prev_value;
  do {
    if (OSAtomicCompareAndSwap32Barrier(old_value, new_value,
                                        const_cast<Atomic32*>(ptr))) {
      return old_value;
    }
    prev_value = *ptr;
  } while (prev_value == old_value);
  return prev_value;
}

inline Atomic32 Release_CompareAndSwap(volatile Atomic32 *ptr,
                                       Atomic32 old_value,
                                       Atomic32 new_value) {
  return Acquire_CompareAndSwap(ptr, old_value, new_value);
}

inline void NoBarrier_Store(volatile Atomic32* ptr, Atomic32 value) {
  *ptr = value;
}

inline void Acquire_Store(volatile Atomic32 *ptr, Atomic32 value) {
  *ptr = value;
  MemoryBarrier();
}

inline void Release_Store(volatile Atomic32 *ptr, Atomic32 value) {
  MemoryBarrier();
  *ptr = value;
}

inline Atomic32 NoBarrier_Load(volatile const Atomic32* ptr) {
  return *ptr;
}

inline Atomic32 Acquire_Load(volatile const Atomic32 *ptr) {
  Atomic32 value = *ptr;
  MemoryBarrier();
  return value;
}

inline Atomic32 Release_Load(volatile const Atomic32 *ptr) {
  MemoryBarrier();
  return *ptr;
}

// 64-bit version

inline Atomic64 NoBarrier_CompareAndSwap(volatile Atomic64 *ptr,
                                         Atomic64 old_value,
                                         Atomic64 new_value) {
  Atomic64 prev_value;
  do {
    if (OSAtomicCompareAndSwap64(old_value, new_value,
                                 const_cast<Atomic64*>(ptr))) {
      return old_value;
    }
    prev_value = *ptr;
  } while (prev_value == old_value);
  return prev_value;
}

inline Atomic64 NoBarrier_AtomicExchange(volatile Atomic64 *ptr,
                                         Atomic64 new_value) {
  Atomic64 old_value;
  do {
    old_value = *ptr;
  } while (!OSAtomicCompareAndSwap64(old_value, new_value,
                                     const_cast<Atomic64*>(ptr)));
  return old_value;
}

inline Atomic64 Acquire_AtomicExchange(volatile Atomic64 *ptr,
                                       Atomic64 new_value) {
  Atomic64 old_value;
  do {
    old_value = *ptr;
  } while (!OSAtomicCompareAndSwap64Barrier(old_value, new_value,
                                            const_cast<Atomic64*>(ptr)));
  return old_value;
}

inline Atomic64 Release_AtomicExchange(volatile Atomic64 *ptr,
                                       Atomic64 new_value) {
  return Acquire_AtomicExchange(ptr, new_value);
}

inline Atomic64 NoBarrier_AtomicIncrement(volatile Atomic64 *ptr,
                                          Atomic64 increment) {
  return OSAtomicAdd64(increment, const_cast<Atomic64*>(ptr));
}

inline Atomic64 Barrier_AtomicIncrement(volatile Atomic64 *ptr,
                                        Atomic64 increment) {
  return OSAtomicAdd64Barrier(increment, const_cast<Atomic64*>(ptr));
}

inline Atomic64 Acquire_CompareAndSwap(volatile Atomic64 *ptr,
                                       Atomic64 old_value,
                                       Atomic64 new_value) {
  Atomic64 prev_value;
  do {
    if (OSAtomicCompareAndSwap64Barrier(old_value, new_value,
                                        const_cast<Atomic64*>(ptr))) {
      return old_value;
    }
    prev_value = *ptr;
  } while (prev_value == old_value);
  return prev_value;
}

inline Atomic64 Release_CompareAndSwap(volatile Atomic64 *ptr,
                                       Atomic64 old_value,
                                       Atomic64 new_value) {
  // The lib kern interface does not distinguish between
  // Acquire and Release memory barriers; they are equivalent.
  return Acquire_CompareAndSwap(ptr, old_value, new_value);
}

#ifdef __LP64__

// 64-bit implementation on 64-bit platform

inline void NoBarrier_Store(volatile Atomic64* ptr, Atomic64 value) {
  *ptr = value;
}

// Issue the x86 "pause" instruction, which tells the CPU that we
// are in a spinlock wait loop and should allow other hyperthreads
// to run, not speculate memory access, etc.
inline void PauseCPU() {
#ifdef __x86_64__
  __asm__ __volatile__("pause" : : : "memory");
#elif defined(__arm64__)
  __asm__ __volatile__("yield" : : : "memory");
#else
  #error "PauseCPU is only supported for x86_64 and arm64 on macOS"
#endif
}

inline void Acquire_Store(volatile Atomic64 *ptr, Atomic64 value) {
  *ptr = value;
  MemoryBarrier();
}

inline void Release_Store(volatile Atomic64 *ptr, Atomic64 value) {
  MemoryBarrier();
  *ptr = value;
}

inline Atomic64 NoBarrier_Load(volatile const Atomic64* ptr) {
  return *ptr;
}

inline Atomic64 Acquire_Load(volatile const Atomic64 *ptr) {
  Atomic64 value = *ptr;
  MemoryBarrier();
  return value;
}

inline Atomic64 Release_Load(volatile const Atomic64 *ptr) {
  MemoryBarrier();
  return *ptr;
}

#else

// 64-bit implementation on 32-bit platform

#if defined(__ppc__)

inline void NoBarrier_Store(volatile Atomic64* ptr, Atomic64 value) {
  __asm__ __volatile__(
      "_NoBarrier_Store_not_supported_for_32_bit_ppc\n\t");
}

inline Atomic64 NoBarrier_Load(volatile const Atomic64* ptr) {
  __asm__ __volatile__(
      "_NoBarrier_Load_not_supported_for_32_bit_ppc\n\t");
  return 0;
}

#elif defined(__i386__)

inline void NoBarrier_Store(volatile Atomic64* ptr, Atomic64 value) {
  __asm__ __volatile__("movq %1, %%mm0\n\t"    // Use mmx reg for 64-bit atomic
                       "movq %%mm0, %0\n\t"  // moves (ptr could be read-only)
                       "emms\n\t"              // Reset FP registers
                       : "=m" (*ptr)
                       : "m" (value)
                       : // mark the FP stack and mmx registers as clobbered
                         "st", "st(1)", "st(2)", "st(3)", "st(4)",
                         "st(5)", "st(6)", "st(7)", "mm0", "mm1",
                         "mm2", "mm3", "mm4", "mm5", "mm6", "mm7");

}

inline Atomic64 NoBarrier_Load(volatile const Atomic64* ptr) {
  Atomic64 value;
  __asm__ __volatile__("movq %1, %%mm0\n\t"  // Use mmx reg for 64-bit atomic
                       "movq %%mm0, %0\n\t"  // moves (ptr could be read-only)
                       "emms\n\t"            // Reset FP registers
                       : "=m" (value)
                       : "m" (*ptr)
                       : // mark the FP stack and mmx registers as clobbered
                         "st", "st(1)", "st(2)", "st(3)", "st(4)",
                         "st(5)", "st(6)", "st(7)", "mm0", "mm1",
                         "mm2", "mm3", "mm4", "mm5", "mm6", "mm7");

  return value;
}

#elif defined(__arm__)

inline void NoBarrier_Store(volatile Atomic64* ptr, Atomic64 value) {
  int store_failed;
  Atomic64 dummy;
  __asm__ __volatile__(
      "1:\n"
      // Dummy load to lock cache line.
      "ldrexd  %1, [%3]\n"
      "strexd  %0, %2, [%3]\n"
      "teq     %0, #0\n"
      "bne     1b"
      : "=&r" (store_failed), "=&r"(dummy)
      : "r"(value), "r" (ptr)
      : "cc", "memory");
}

inline Atomic64 NoBarrier_Load(volatile const Atomic64* ptr) {
  Atomic64 res;
  __asm__ __volatile__(
      "ldrexd   %0, [%1]\n"
      "clrex\n"
      : "=r" (res)
      : "r"(ptr), "Q"(*ptr));
  return res;
}

#endif


inline void Acquire_Store(volatile Atomic64 *ptr, Atomic64 value) {
  NoBarrier_Store(ptr, value);
  MemoryBarrier();
}

inline void Release_Store(volatile Atomic64 *ptr, Atomic64 value) {
  MemoryBarrier();
  NoBarrier_Store(ptr, value);
}

inline Atomic64 Acquire_Load(volatile const Atomic64 *ptr) {
  Atomic64 value = NoBarrier_Load(ptr);
  MemoryBarrier();
  return value;
}

inline Atomic64 Release_Load(volatile const Atomic64 *ptr) {
  MemoryBarrier();
  return NoBarrier_Load(ptr);
}
#endif  // __LP64__

}   // namespace subtle
}   // namespace base

// NOTE(user): The following is also deprecated.  New callers should use
// the base::subtle namespace.
inline void MemoryBarrier() {
  base::subtle::MemoryBarrier();
}
