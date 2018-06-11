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
#ifndef YB_CLIENT_BATCHER_H_
#define YB_CLIENT_BATCHER_H_

#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "yb/client/async_rpc.h"
#include "yb/client/transaction.h"

#include "yb/common/consistent_read_point.h"
#include "yb/common/transaction.h"

#include "yb/gutil/gscoped_ptr.h"
#include "yb/gutil/macros.h"
#include "yb/gutil/ref_counted.h"

#include "yb/util/async_util.h"
#include "yb/util/atomic.h"
#include "yb/util/debug-util.h"
#include "yb/util/locks.h"
#include "yb/util/status.h"

namespace yb {

namespace client {

class YBClient;
class YBSession;
class YBStatusCallback;
class YBOperation;

namespace internal {

struct InFlightOp;

class Batcher;
class ErrorCollector;
class RemoteTablet;
class AsyncRpc;

// A Batcher is the class responsible for collecting row operations, routing them to the
// correct tablet server, and possibly batching them together for better efficiency.
//
// It is a reference-counted class: the client session creating the batch holds one
// reference, and all of the in-flight operations hold others. This allows the client
// session to be destructed while ops are still in-flight, without the async callbacks
// attempting to access a destructed Batcher.
class Batcher : public RefCountedThreadSafe<Batcher> {
 public:
  // Create a new batcher associated with the given session.
  //
  // Any errors which come back from operations performed by this batcher are posted to
  // the provided ErrorCollector.
  //
  // Takes a reference on error_collector. Creates a weak_ptr to 'session'.
  Batcher(YBClient* client,
          ErrorCollector* error_collector,
          const std::shared_ptr<YBSessionData>& session,
          YBTransactionPtr transaction,
          ConsistentReadPoint* read_point);

  // Abort the current batch. Any writes that were buffered and not yet sent are
  // discarded. Those that were sent may still be delivered.  If there is a pending Flush
  // callback, it will be called immediately with an error status.
  void Abort(const Status& status);

  // Set the timeout for this batcher.
  //
  // The timeout is currently set on all of the RPCs, but in the future will be relative
  // to when the Flush call is made (eg even if the lookup of the TS takes a long time, it
  // may time out before even sending an op). TODO: implement that
  void SetTimeout(MonoDelta timeout);

  // Add a new operation to the batch. Requires that the batch has not yet been flushed.
  // TODO: in other flush modes, this may not be the case -- need to
  // update this when they're implemented.
  //
  // NOTE: If this returns not-OK, does not take ownership of 'write_op'.
  CHECKED_STATUS Add(std::shared_ptr<YBOperation> yb_op) WARN_UNUSED_RESULT;

  // Return true if any operations are still pending. An operation is no longer considered
  // pending once it has either errored or succeeded.  Operations are considering pending
  // as soon as they are added, even if Flush has not been called.
  bool HasPendingOperations() const;

  // Return the number of buffered operations. These are only those operations which are
  // "corked" (i.e not yet flushed). Once Flush has been called, this returns 0.
  int CountBufferedOperations() const;

  // Flush any buffered operations. The callback will be called once there are no
  // more pending operations from this Batcher. If all of the operations succeeded,
  // then the callback will receive Status::OK. Otherwise, it will receive IOError,
  // and the caller must inspect the ErrorCollector to retrieve more detailed
  // information on which operations failed.
  void FlushAsync(boost::function<void(const Status&)> callback);

  MonoTime deadline() const {
    return deadline_;
  }

  const std::shared_ptr<rpc::Messenger>& messenger() const;

  rpc::ProxyCache& proxy_cache() const;

  const std::shared_ptr<AsyncRpcMetrics>& async_rpc_metrics() const {
    return async_rpc_metrics_;
  }

  ConsistentReadPoint* read_point() {
    return read_point_;
  }

  YBTransactionPtr transaction() const;

  const TransactionMetadata& transaction_metadata() const {
    return transaction_metadata_;
  }

  void set_allow_local_calls_in_curr_thread(bool flag) { allow_local_calls_in_curr_thread_ = flag; }

  bool allow_local_calls_in_curr_thread() const { return allow_local_calls_in_curr_thread_; }

 private:
  friend class RefCountedThreadSafe<Batcher>;
  friend class AsyncRpc;
  friend class WriteRpc;
  friend class ReadRpc;

  ~Batcher();

  // Add an op to the in-flight set and increment the ref-count.
  void AddInFlightOp(const InFlightOpPtr& op);

  void RemoveInFlightOpsAfterFlushing(
      const InFlightOps& ops, const Status& status, HybridTime propagated_hybrid_time);

    // Return true if the batch has been aborted, and any in-flight ops should stop
  // processing wherever they are.
  bool IsAbortedUnlocked() const;

  // Mark the fact that errors have occurred with this batch. This ensures that
  // the flush callback will get a bad Status.
  void MarkHadErrors();

  // Remove an op from the in-flight op list, and delete the op itself.
  // The operation is reported to the ErrorReporter as having failed with the
  // given status.
  void MarkInFlightOpFailed(const InFlightOpPtr& op, const Status& s);
  void MarkInFlightOpFailedUnlocked(const InFlightOpPtr& in_flight_op, const Status& s);

  void CheckForFinishedFlush();
  void FlushBuffersIfReady();
  void FlushBuffer(
      RemoteTablet* tablet, InFlightOps::const_iterator begin, InFlightOps::const_iterator end,
      const bool allow_local_calls_in_curr_thread);

  // Calls/Schedules flush_callback_ and resets it to free resources.
  void RunCallback(const Status& s);

  // Log an error where an Rpc callback has response count mismatch.
  void AddOpCountMismatchError();

  // Cleans up an RPC response, scooping out any errors and passing them up
  // to the batcher.
  void ProcessReadResponse(const ReadRpc &rpc, const Status &s);
  void ProcessWriteResponse(const WriteRpc &rpc, const Status &s);

  // Process RPC status.
  void ProcessRpcStatus(const AsyncRpc &rpc, const Status &s);

  // Async Callbacks.
  void TabletLookupFinished(InFlightOpPtr op, const Status& s);

  // Compute a new deadline based on timeout_. If no timeout_ has been set,
  // uses a hard-coded default and issues periodic warnings.
  MonoTime ComputeDeadlineUnlocked() const;

  void TransactionReady(const Status& status, const BatcherPtr& self);

  // See note about lock ordering in batcher.cc
  mutable simple_spinlock lock_;

  enum State {
    kGatheringOps,
    kFlushing,
    kFlushed,
    kAborted
  };
  State state_;

  YBClient* const client_;
  std::weak_ptr<YBSessionData> weak_session_data_;

  // Errors are reported into this error collector.
  scoped_refptr<ErrorCollector> const error_collector_;

  // Set to true if there was at least one error from this Batcher.
  // Protected by lock_
  bool had_errors_;

  // If state is kFlushing, this member will be set to the user-provided
  // callback. Once there are no more in-flight operations, the callback
  // will be called exactly once (and the state changed to kFlushed).
  boost::function<void(const Status&)> flush_callback_;

  // All buffered or in-flight ops.
  // Added to this set during apply, removed during Finished of AsyncRpc.
  std::unordered_set<InFlightOpPtr> ops_;
  InFlightOps ops_queue_;

  // When each operation is added to the batcher, it is assigned a sequence number
  // which preserves the user's intended order. Preserving order is critical when
  // a batch contains multiple operations against the same row key. This member
  // assigns the sequence numbers.
  // Protected by lock_.
  int next_op_sequence_number_;

  // Amount of time to wait for a given op, from start to finish.
  //
  // Set by SetTimeout.
  MonoDelta timeout_;

  // After flushing, the absolute deadline for all in-flight ops.
  MonoTime deadline_;

  // Number of outstanding lookups across all in-flight ops.
  int outstanding_lookups_ = 0;

  // The maximum number of bytes of encoded operations which will be allowed to
  // be buffered.
  int64_t max_buffer_size_;

  // If true, we might allow the local calls to be run in the same IPC thread.
  bool allow_local_calls_in_curr_thread_ = true;

  // The number of bytes used in the buffer for pending operations.
  AtomicInt<int64_t> buffer_bytes_used_;

  std::shared_ptr<yb::client::internal::AsyncRpcMetrics> async_rpc_metrics_;

  YBTransactionPtr transaction_;

  TransactionMetadata transaction_metadata_;

  // The consistent read point for this batch if it is specified.
  ConsistentReadPoint* read_point_ = nullptr;

  DISALLOW_COPY_AND_ASSIGN(Batcher);
};

}  // namespace internal
}  // namespace client
}  // namespace yb
#endif  // YB_CLIENT_BATCHER_H_
