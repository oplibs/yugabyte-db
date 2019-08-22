// Copyright (c) 2011 The LevelDB Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file. See the AUTHORS file for names of contributors.
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
// A Status encapsulates the result of an operation.  It may indicate success,
// or it may indicate an error with an associated error message.
//
// Multiple threads can invoke const methods on a Status without
// external synchronization, but if any of the threads may call a
// non-const method, all threads accessing the same Status must use
// external synchronization.

#ifndef YB_UTIL_STATUS_H_
#define YB_UTIL_STATUS_H_

#include <atomic>
#include <memory>
#include <string>

#include <boost/intrusive_ptr.hpp>

#include <boost/preprocessor/cat.hpp>
#include <boost/preprocessor/seq/for_each.hpp>
#include <boost/preprocessor/tuple/elem.hpp>

#ifdef YB_HEADERS_NO_STUBS
#include "yb/gutil/macros.h"
#include "yb/gutil/port.h"
#else
#include "yb/client/stubs.h"
#endif

#include "yb/util/slice.h"
#include "yb/util/format.h"
#include "yb/util/strongly_typed_bool.h"
#include "yb/gutil/strings/substitute.h"

// Return the given status if it is not OK.
#define YB_RETURN_NOT_OK(s) do { \
    auto&& _s = (s); \
    if (PREDICT_FALSE(!_s.ok())) return MoveStatus(std::move(_s)); \
  } while (false)

// Return the given status if it is not OK, but first clone it and prepend the given message.
#define YB_RETURN_NOT_OK_PREPEND(s, msg) do { \
    auto&& _s = (s); \
    if (PREDICT_FALSE(!_s.ok())) return MoveStatus(_s).CloneAndPrepend(msg); \
  } while (0);

// Return 'to_return' if 'to_call' returns a bad status.  The substitution for 'to_return' may
// reference the variable 's' for the bad status.
#define YB_RETURN_NOT_OK_RET(to_call, to_return) do { \
    ::yb::Status s = (to_call); \
    if (PREDICT_FALSE(!s.ok())) return (to_return);  \
  } while (0);

#define YB_DFATAL_OR_RETURN_NOT_OK(s) do { \
    LOG_IF(DFATAL, !s.ok()) << s; \
    YB_RETURN_NOT_OK(s); \
  } while (0);

#define YB_DFATAL_OR_RETURN_ERROR_IF(condition, s) do { \
    if (PREDICT_FALSE(condition)) { \
      DCHECK(!s.ok()) << "Invalid OK status"; \
      LOG(DFATAL) << s; \
      return s; \
    } \
  } while (0);

// Emit a warning if 'to_call' returns a bad status.
#define YB_WARN_NOT_OK(to_call, warning_prefix) do { \
    ::yb::Status _s = (to_call); \
    if (PREDICT_FALSE(!_s.ok())) { \
      YB_LOG(WARNING) << (warning_prefix) << ": " << _s.ToString();  \
    } \
  } while (0);

#define WARN_WITH_PREFIX_NOT_OK(to_call, warning_prefix) do { \
    ::yb::Status _s = (to_call); \
    if (PREDICT_FALSE(!_s.ok())) { \
      YB_LOG(WARNING) << LogPrefix() << (warning_prefix) << ": " << _s; \
    } \
  } while (0);

// Emit a error if 'to_call' returns a bad status.
#define ERROR_NOT_OK(to_call, error_prefix) do { \
    ::yb::Status _s = (to_call); \
    if (PREDICT_FALSE(!_s.ok())) { \
      YB_LOG(ERROR) << (error_prefix) << ": " << _s.ToString();  \
    } \
  } while (0);

// Log the given status and return immediately.
#define YB_LOG_AND_RETURN(level, status) do { \
    ::yb::Status _s = (status); \
    YB_LOG(level) << _s.ToString(); \
    return _s; \
  } while (0);

// If 'to_call' returns a bad status, CHECK immediately with a logged message of 'msg' followed by
// the status.
#define YB_CHECK_OK_PREPEND(to_call, msg) do { \
  auto&& _s = (to_call); \
  YB_CHECK(_s.ok()) << (msg) << ": " << StatusToString(_s); \
  } while (0);

// If the status is bad, CHECK immediately, appending the status to the logged message.
#define YB_CHECK_OK(s) YB_CHECK_OK_PREPEND(s, "Bad status")

#define RETURN_NOT_OK           YB_RETURN_NOT_OK
#define RETURN_NOT_OK_PREPEND   YB_RETURN_NOT_OK_PREPEND
#define RETURN_NOT_OK_RET       YB_RETURN_NOT_OK_RET
// If status is not OK, this will FATAL in debug mode, or return the error otherwise.
#define DFATAL_OR_RETURN_NOT_OK YB_DFATAL_OR_RETURN_NOT_OK
#define DFATAL_OR_RETURN_ERROR_IF  YB_DFATAL_OR_RETURN_ERROR_IF
#define WARN_NOT_OK             YB_WARN_NOT_OK
#define LOG_AND_RETURN          YB_LOG_AND_RETURN
#define CHECK_OK_PREPEND        YB_CHECK_OK_PREPEND
#define CHECK_OK                YB_CHECK_OK

// These are standard glog macros.
#define YB_LOG              LOG
#define YB_CHECK            CHECK

extern "C" {

struct YBCStatusStruct;

}

namespace yb {

#define YB_STATUS_CODES \
    ((Ok, OK, 0, "OK")) \
    ((NotFound, NOT_FOUND, 1, "Not found")) \
    ((Corruption, CORRUPTION, 2, "Corruption")) \
    ((NotSupported, NOT_SUPPORTED, 3, "Not implemented")) \
    ((InvalidArgument, INVALID_ARGUMENT, 4, "Invalid argument")) \
    ((IOError, IO_ERROR, 5, "IO error")) \
    ((AlreadyPresent, ALREADY_PRESENT, 6, "Already present")) \
    ((RuntimeError, RUNTIME_ERROR, 7, "Runtime error")) \
    ((NetworkError, NETWORK_ERROR, 8, "Network error")) \
    ((IllegalState, ILLEGAL_STATE, 9, "Illegal state")) \
    ((NotAuthorized, NOT_AUTHORIZED, 10, "Not authorized")) \
    ((Aborted, ABORTED, 11, "Aborted")) \
    ((RemoteError, REMOTE_ERROR, 12, "Remote error")) \
    ((ServiceUnavailable, SERVICE_UNAVAILABLE, 13, "Service unavailable")) \
    ((TimedOut, TIMED_OUT, 14, "Timed out")) \
    ((Uninitialized, UNINITIALIZED, 15, "Uninitialized")) \
    ((ConfigurationError, CONFIGURATION_ERROR, 16, "Configuration error")) \
    ((Incomplete, INCOMPLETE, 17, "Incomplete")) \
    ((EndOfFile, END_OF_FILE, 18, "End of file")) \
    ((InvalidCommand, INVALID_COMMAND, 19, "Invalid command")) \
    ((QLError, QL_ERROR, 20, "Query error")) \
    ((InternalError, INTERNAL_ERROR, 21, "Internal error")) \
    ((Expired, EXPIRED, 22, "Operation expired")) \
    ((LeaderNotReadyToServe, LEADER_NOT_READY_TO_SERVE, 23, \
        "Leader not ready to serve requests.")) \
    ((LeaderHasNoLease, LEADER_HAS_NO_LEASE, 24, "Leader does not have a valid lease.")) \
    ((TryAgain, TRY_AGAIN_CODE, 25, "Operation failed. Try again.")) \
    ((Busy, BUSY, 26, "Resource busy")) \
    ((ShutdownInProgress, SHUTDOWN_IN_PROGRESS, 27, "Shutdown in progress")) \
    ((MergeInProgress, MERGE_IN_PROGRESS, 28, "Merge in progress")) \
    ((Combined, COMBINED_ERROR, 29, "Combined status representing multiple status failures.")) \
    ((SnapshotTooOld, SNAPSHOT_TOO_OLD, 30, "Snapshot too old")) \
    /**/

#define YB_STATUS_CODE_DECLARE(name, pb_name, value, message) \
    BOOST_PP_CAT(k, name) = value,

#define YB_STATUS_CODE_IS_FUNC(name, pb_name, value, message) \
    bool BOOST_PP_CAT(Is, name)() const { \
      return code() == BOOST_PP_CAT(k, name); \
    } \
    /**/

#define YB_STATUS_FORWARD_MACRO(r, data, tuple) data tuple

enum class TimeoutError {
  kMutexTimeout = 1,
  kLockTimeout = 2,
  kLockLimit = 3,
};

YB_STRONGLY_TYPED_BOOL(DupFileName);
YB_STRONGLY_TYPED_BOOL(AddRef);

class Status {
 public:
  // Create a success status.
  Status() {}

  // Return a success status.
  static Status OK() { return Status(); }

  // Returns true if the status indicates success.
  bool ok() const { return state_ == nullptr; }

  // Declares set of Is* functions
  BOOST_PP_SEQ_FOR_EACH(YB_STATUS_FORWARD_MACRO, YB_STATUS_CODE_IS_FUNC, YB_STATUS_CODES)

  // Returns a text message of this status to be reported to users.
  // Returns empty string for success.
  std::string ToUserMessage(bool include_code = false) const;

  // Return a string representation of this status suitable for printing.
  // Returns the string "OK" for success.
  std::string ToString(bool include_file_and_line = true, bool include_code = true) const;

  // Return a string representation of the status code, without the message
  // text or posix code information.
  std::string CodeAsString() const;

  // Returned string has unlimited lifetime, and should NOT be released by the caller.
  const char* CodeAsCString() const;

  // Return the message portion of the Status. This is similar to ToString,
  // except that it does not include the stringified error code or posix code.
  //
  // For OK statuses, this returns an empty string.
  //
  // The returned Slice is only valid as long as this Status object remains
  // live and unchanged.
  Slice message() const;

  int64_t error_code() const { return state_ ? state_->error_code : 0; }

  const char* file_name() const { return state_ ? state_->file_name : ""; }
  int line_number() const { return state_ ? state_->line_number : 0; }

  // Return a new Status object with the same state plus an additional leading message.
  Status CloneAndPrepend(const Slice& msg) const;

  // Same as CloneAndPrepend, but appends to the message instead.
  Status CloneAndAppend(const Slice& msg) const;

  Status CloneAndChangeErrorCode(int64_t error_code) const;

  // Returns the memory usage of this object without the object itself. Should
  // be used when embedded inside another object.
  size_t memory_footprint_excluding_this() const;

  // Returns the memory usage of this object including the object itself.
  // Should be used when allocated on the heap.
  size_t memory_footprint_including_this() const;

  enum Code : int32_t {
    BOOST_PP_SEQ_FOR_EACH(YB_STATUS_FORWARD_MACRO, YB_STATUS_CODE_DECLARE, YB_STATUS_CODES)

    // NOTE: Remember to duplicate these constants into wire_protocol.proto and
    // and to add StatusTo/FromPB ser/deser cases in wire_protocol.cc !
    //
    // TODO: Move error codes into an error_code.proto or something similar.
  };

  Status(Code code,
         const char* file_name,
         int line_number,
         const Slice& msg,
         // Error message details. If present - would be combined as "msg: msg2".
         const Slice& msg2 = Slice(),
         int64_t error_code = -1,
         DupFileName dup_file_name = DupFileName::kFalse);

  Status(Code code,
         const char* file_name,
         int line_number,
         TimeoutError error_code);

  Code code() const {
    return (state_ == nullptr) ? kOk : static_cast<Code>(state_->code);
  }

  // Adopt status that was previously exported to C interface.
  explicit Status(YBCStatusStruct* state, AddRef add_ref);

  // Increments state ref count and returns pointer that could be used in C interface.
  YBCStatusStruct* RetainStruct() const;

  // Reset state w/o touching ref count. Return detached pointer that could be used in C interface.
  YBCStatusStruct* DetachStruct();

 private:
  struct State {
    State(const State&) = delete;
    void operator=(const State&) = delete;

    std::atomic<size_t> counter;
    uint32_t message_len;
    uint8_t code;
    int64_t error_code;
    // This must always be a pointer to a constant string.
    // The status object does not own this string.
    const char* file_name;
    int line_number;
    char message[1];
  };

  bool file_name_duplicated() const;

  typedef boost::intrusive_ptr<State> StatePtr;

  friend inline void intrusive_ptr_release(State* state) {
    if (state->counter.fetch_sub(1, std::memory_order_acq_rel) == 1) {
      free(state);
    }
  }

  friend inline void intrusive_ptr_add_ref(State* state) {
    state->counter.fetch_add(1, std::memory_order_relaxed);
  }

  StatePtr state_;
  static constexpr size_t kHeaderSize = offsetof(State, message);

  static_assert(sizeof(Code) == 4, "Code enum size is part of ABI");
};

inline Status&& MoveStatus(Status&& status) {
  return std::move(status);
}

inline const Status& MoveStatus(const Status& status) {
  return status;
}

inline std::string StatusToString(const Status& status) {
  return status.ToString();
}

inline std::ostream& operator<<(std::ostream& out, const Status& status) {
  return out << status.ToString();
}

}  // namespace yb

#define STATUS(status_type, ...) \
    (Status(Status::BOOST_PP_CAT(k, status_type), __FILE__, __LINE__, __VA_ARGS__))
#define STATUS_SUBSTITUTE(status_type, ...) \
    (Status(Status::BOOST_PP_CAT(k, status_type), \
            __FILE__, \
            __LINE__, \
            strings::Substitute(__VA_ARGS__)))

#define STATUS_FORMAT(status_type, ...) \
    (::yb::Status(::yb::Status::BOOST_PP_CAT(k, status_type), \
            __FILE__, \
            __LINE__, \
            ::yb::Format(__VA_ARGS__)))

// Utility macros to perform the appropriate check. If the check fails,
// returns the specified (error) Status, with the given message.
#define SCHECK_OP(var1, op, var2, type, msg) \
  do { \
    auto v1_tmp = (var1); \
    auto v2_tmp = (var2); \
    if (PREDICT_FALSE(!((v1_tmp)op(v2_tmp)))) return STATUS(type, \
      yb::Format("$0: $1 vs. $2", (msg), v1_tmp, v2_tmp)); \
  } while (0)
#define SCHECK(expr, type, msg) SCHECK_OP(expr, ==, true, type, msg)
#define SCHECK_EQ(var1, var2, type, msg) SCHECK_OP(var1, ==, var2, type, msg)
#define SCHECK_NE(var1, var2, type, msg) SCHECK_OP(var1, !=, var2, type, msg)
#define SCHECK_GT(var1, var2, type, msg) SCHECK_OP(var1, >, var2, type, msg)  // NOLINT.
#define SCHECK_GE(var1, var2, type, msg) SCHECK_OP(var1, >=, var2, type, msg)
#define SCHECK_LT(var1, var2, type, msg) SCHECK_OP(var1, <, var2, type, msg)  // NOLINT.
#define SCHECK_LE(var1, var2, type, msg) SCHECK_OP(var1, <=, var2, type, msg)
#define SCHECK_BOUNDS(var1, lbound, rbound, type, msg) \
    do { \
      SCHECK_GE(var1, lbound, type, msg); \
      SCHECK_LE(var1, rbound, type, msg); \
    } while(false)

#ifndef NDEBUG

#define DSCHECK(expr, type, msg) SCHECK(expr, type, msg)
#define DSCHECK_EQ(var1, var2, type, msg) SCHECK_EQ(var1, var2, type, msg)
#define DSCHECK_NE(var1, var2, type, msg) SCHECK_NE(var1, var2, type, msg)
#define DSCHECK_GT(var1, var2, type, msg) SCHECK_GT(var1, var2, type, msg)
#define DSCHECK_GE(var1, var2, type, msg) SCHECK_GE(var1, var2, type, msg)
#define DSCHECK_LT(var1, var2, type, msg) SCHECK_LT(var1, var2, type, msg)
#define DSCHECK_LE(var1, var2, type, msg) SCHECK_LE(var1, var2, type, msg)

#else

#define DSCHECK(expr, type, msg) DCHECK(expr) << msg
#define DSCHECK_EQ(var1, var2, type, msg) DCHECK_EQ(var1, var2) << msg
#define DSCHECK_NE(var1, var2, type, msg) DCHECK_NE(var1, var2) << msg
#define DSCHECK_GT(var1, var2, type, msg) DCHECK_GT(var1, var2) << msg
#define DSCHECK_GE(var1, var2, type, msg) DCHECK_GE(var1, var2) << msg
#define DSCHECK_LT(var1, var2, type, msg) DCHECK_LT(var1, var2) << msg
#define DSCHECK_LE(var1, var2, type, msg) DCHECK_LE(var1, var2) << msg

#endif

#ifdef YB_HEADERS_NO_STUBS
#define CHECKED_STATUS MUST_USE_RESULT ::yb::Status
#else
// Only for the build using client headers. MUST_USE_RESULT is undefined in that case.
#define CHECKED_STATUS ::yb::Status
#endif

#endif  // YB_UTIL_STATUS_H_
