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

#include "yb/client/transaction.h"

#include <unordered_set>

#include "yb/client/async_rpc.h"
#include "yb/client/client.h"
#include "yb/client/in_flight_op.h"
#include "yb/client/meta_cache.h"
#include "yb/client/tablet_rpc.h"
#include "yb/client/transaction_manager.h"
#include "yb/client/transaction_rpc.h"
#include "yb/client/yb_op.h"

#include "yb/common/transaction.h"

#include "yb/rpc/messenger.h"
#include "yb/rpc/rpc.h"
#include "yb/rpc/scheduler.h"

#include "yb/util/logging.h"
#include "yb/util/random_util.h"
#include "yb/util/result.h"
#include "yb/util/strongly_typed_bool.h"

using namespace std::literals;
using namespace std::placeholders;

DEFINE_uint64(transaction_heartbeat_usec, 500000, "Interval of transaction heartbeat in usec.");
DEFINE_bool(transaction_disable_heartbeat_in_tests, false, "Disable heartbeat during test.");
DEFINE_bool(transaction_disable_proactive_cleanup_in_tests, false,
            "Disable cleanup of intents in abort path.");
DECLARE_uint64(max_clock_skew_usec);

namespace yb {
namespace client {

namespace {

YB_STRONGLY_TYPED_BOOL(Child);
YB_DEFINE_ENUM(TransactionState, (kRunning)(kAborted)(kCommitted));

} // namespace

Result<ChildTransactionData> ChildTransactionData::FromPB(const ChildTransactionDataPB& data) {
  ChildTransactionData result;
  auto metadata = TransactionMetadata::FromPB(data.metadata());
  RETURN_NOT_OK(metadata);
  result.metadata = std::move(*metadata);
  result.read_time = ReadHybridTime::FromReadTimePB(data);
  for (const auto& entry : data.local_limits()) {
    result.local_limits.emplace(entry.first, HybridTime(entry.second));
  }
  return result;
}

YB_DEFINE_ENUM(MetadataState, (kMissing)(kMaybePresent)(kPresent));

class YBTransaction::Impl final {
 public:
  Impl(TransactionManager* manager, YBTransaction* transaction)
      : manager_(manager),
        transaction_(transaction),
        read_point_(manager->clock()),
        child_(Child::kFalse) {
    metadata_.transaction_id = GenerateTransactionId();
    metadata_.priority = RandomUniformInt<uint64_t>();
    CompleteConstruction();
    VLOG_WITH_PREFIX(2) << "Started, metadata: " << metadata_;
  }

  Impl(TransactionManager* manager, YBTransaction* transaction, ChildTransactionData data)
      : manager_(manager),
        transaction_(transaction),
        read_point_(manager->clock()),
        child_(Child::kTrue),
        child_had_read_time_(data.read_time) {
    // For serializable isolation we use read intents, so could always read most recent
    // version of DB.
    // Otherwise there is possible case when we miss value change that happened after transaction
    // start.
    if (data.metadata.isolation == IsolationLevel::SNAPSHOT_ISOLATION) {
      read_point_.SetReadTime(std::move(data.read_time), std::move(data.local_limits));
    }
    metadata_ = std::move(data.metadata);
    CompleteConstruction();
    VLOG_WITH_PREFIX(2) << "Started child, metadata: " << metadata_;
    ready_ = true;
  }

  ~Impl() {
    manager_->rpcs().Abort({&heartbeat_handle_, &commit_handle_, &abort_handle_});
    LOG_IF_WITH_PREFIX(DFATAL, !waiters_.empty()) << "Non empty waiters";
  }

  YBTransactionPtr CreateSimilarTransaction() {
    return std::make_shared<YBTransaction>(manager_);
  }

  CHECKED_STATUS Init(IsolationLevel isolation, const ReadHybridTime& read_time) {
    if (read_point_.GetReadTime().read.is_valid()) {
      return STATUS_FORMAT(IllegalState, "Read point already specified: $0",
                           read_point_.GetReadTime());
    }

    if (read_time.read.is_valid()) {
      read_point_.SetReadTime(read_time, ConsistentReadPoint::HybridTimeMap());
    }
    metadata_.isolation = isolation;
    if (read_point_.GetReadTime()) {
      metadata_.DEPRECATED_start_time = read_point_.GetReadTime().read;
    } else {
      metadata_.DEPRECATED_start_time = read_point_.Now();
    }

    return Status::OK();
  }

  void InitWithReadPoint(IsolationLevel isolation, ConsistentReadPoint&& read_point) {
    metadata_.isolation = isolation;
    read_point_ = std::move(read_point);
  }

  const IsolationLevel isolation() const {
    return metadata_.isolation;
  }

  // This transaction is a restarted transaction, so we set it up with data from original one.
  CHECKED_STATUS FillRestartedTransaction(Impl* other) {
    VLOG_WITH_PREFIX(1) << "Setup restart to " << other->ToString();
    auto transaction = transaction_->shared_from_this();
    {
      std::lock_guard<std::mutex> lock(mutex_);
      auto state = state_.load(std::memory_order_acquire);
      if (state != TransactionState::kRunning) {
        return STATUS_FORMAT(
            IllegalState, "Restart of completed transaction $0: $1",
            metadata_.transaction_id, state);
      }
      if (!read_point_.IsRestartRequired()) {
        return STATUS_FORMAT(
            IllegalState, "Restart of transaction that does not require restart: $0",
            metadata_.transaction_id);
      }
      other->read_point_ = std::move(read_point_);
      other->read_point_.Restart();
      other->metadata_.isolation = metadata_.isolation;
      if (metadata_.isolation == IsolationLevel::SNAPSHOT_ISOLATION) {
        other->metadata_.DEPRECATED_start_time = other->read_point_.GetReadTime().read;
      } else {
        other->metadata_.DEPRECATED_start_time = other->read_point_.Now();
      }
      state_.store(TransactionState::kAborted, std::memory_order_release);
    }
    DoAbort(TransactionRpcDeadline(), Status::OK(), transaction);

    return Status::OK();
  }

  bool Prepare(const internal::InFlightOps& ops,
               ForceConsistentRead force_consistent_read,
               CoarseTimePoint deadline,
               Waiter waiter,
               TransactionMetadata* metadata) {
    VLOG_WITH_PREFIX(2) << "Prepare";

    bool has_tablets_without_metadata = false;
    {
      std::unique_lock<std::mutex> lock(mutex_);
      if (!ready_) {
        if (waiter) {
          waiters_.push_back(std::move(waiter));
        }
        lock.unlock();
        RequestStatusTablet(deadline);
        VLOG_WITH_PREFIX(2) << "Prepare, rejected (not ready, requesting status tablet)";
        return false;
      }

      bool single_tablet = true;
      internal::RemoteTablet* tablet = nullptr;
      for (auto op_it = ops.begin(); op_it != ops.end();) {
        single_tablet = tablet == nullptr;
        tablet = (**op_it).tablet.get();
        bool will_write_metadata = false;
        for (;;) {
          DCHECK((**op_it).tablet != nullptr);
          if (!will_write_metadata &&
              (!(**op_it).yb_op->read_only() ||
               metadata_.isolation == IsolationLevel::SERIALIZABLE_ISOLATION)) {
            will_write_metadata = true;
          }
          if (++op_it == ops.end() || (**op_it).tablet.get() != tablet) {
            break;
          }
        }

        auto it = tablets_.find(tablet->tablet_id());
        if (it == tablets_.end()) {
          it = tablets_.emplace(tablet->tablet_id(), TabletState()).first;
          has_tablets_without_metadata = true;
        } else {
          // It is possible that after restart tablet does not know that he already stored metadata
          // so we should tell him, that he should check RocksDB before creating transaction state
          // from received metadata.
          //
          // It is important because otherwise he could lose write id.
          //
          // Also it is better to avoid doing such check each time, because it has significant
          // impact on performance. So we are doing this optimization.
          auto metadata_state = it->second.metadata_state;
          has_tablets_without_metadata =
              has_tablets_without_metadata ||
              metadata_state != InvolvedTabletMetadataState::EXIST;
        }
        // Prepare is invoked when we are going to send request to tablet server.
        // So after that tablet may have metadata, and we reflect it in our local state.
        if (it->second.metadata_state == InvolvedTabletMetadataState::MISSING &&
            will_write_metadata) {
          it->second.metadata_state = InvolvedTabletMetadataState::MAY_EXIST;
        }
      }

      // For serializable isolation we never choose read time, since it always reads latest
      // snapshot.
      // For snapshot isolation, if read time was not yet picked, we have to choose it now, if there
      // multiple tablets that will process first request.
      SetReadTimeIfNeeded(!single_tablet || force_consistent_read);
    }

    VLOG_WITH_PREFIX(3) << "Prepare, has_tablets_without_metadata: "
                        << has_tablets_without_metadata;
    if (metadata) {
      if (has_tablets_without_metadata) {
        *metadata = metadata_;
      } else {
        metadata->transaction_id = metadata_.transaction_id;
      }
    }

    return true;
  }

  void Flushed(
      const internal::InFlightOps& ops, const ReadHybridTime& used_read_time,
      const Status& status) {
    VLOG_WITH_PREFIX(5)
        << "Flushed: " << yb::ToString(ops) << ", used_read_time: " << used_read_time
        << ", status: " << status;

    if (status.ok()) {
      std::lock_guard<std::mutex> lock(mutex_);
      if (used_read_time && metadata_.isolation == IsolationLevel::SNAPSHOT_ISOLATION) {
        LOG_IF_WITH_PREFIX(DFATAL, read_point_.GetReadTime())
            << "Read time already picked (" << read_point_.GetReadTime()
            << ", but server replied with used read time: " << used_read_time;
        read_point_.SetReadTime(used_read_time, ConsistentReadPoint::HybridTimeMap());
      }
      TabletStates::iterator it = tablets_.end();
      for (const auto& op : ops) {
        if (op->yb_op->wrote_data(metadata_.isolation)) {
          const std::string& tablet_id = op->tablet->tablet_id();
          // Usually all ops belong to the same tablet. So we can avoid repeating lookup.
          if (it == tablets_.end() || it->first != tablet_id) {
            auto it = tablets_.find(tablet_id);
            CHECK(it != tablets_.end());
            it->second.metadata_state = InvolvedTabletMetadataState::EXIST;
          }
        }
      }
    } else if (status.IsTryAgain()) {
      SetError(status);
    }
    // We should not handle other errors, because it is just notification that batch was failed.
    // And they are handled during processing of that batch.
  }

  void Commit(CoarseTimePoint deadline, CommitCallback callback) {
    auto transaction = transaction_->shared_from_this();
    {
      std::unique_lock<std::mutex> lock(mutex_);
      auto status = CheckRunning(&lock);
      if (!status.ok()) {
        callback(status);
        return;
      }
      if (child_) {
        callback(STATUS(IllegalState, "Commit of child transaction is not allowed"));
        return;
      }
      if (IsRestartRequired()) {
        callback(STATUS(
            IllegalState, "Commit of transaction that requires restart is not allowed"));
        return;
      }
      state_.store(TransactionState::kCommitted, std::memory_order_release);
      commit_callback_ = std::move(callback);
      if (!ready_) {
        waiters_.emplace_back(std::bind(&Impl::DoCommit, this, deadline, _1, transaction));
        lock.unlock();
        RequestStatusTablet(deadline);
        return;
      }
    }
    DoCommit(deadline, Status::OK(), transaction);
  }

  void Abort(CoarseTimePoint deadline) {
    auto transaction = transaction_->shared_from_this();
    {
      std::unique_lock<std::mutex> lock(mutex_);
      auto state = state_.load(std::memory_order_acquire);
      if (state != TransactionState::kRunning) {
        LOG_IF(DFATAL, state != TransactionState::kAborted) << "Abort of committed transaction";
        return;
      }
      if (child_) {
        LOG(DFATAL) << "Abort of child transaction";
        return;
      }
      state_.store(TransactionState::kAborted, std::memory_order_release);
      if (!ready_) {
        waiters_.emplace_back(std::bind(&Impl::DoAbort, this, deadline, _1, transaction));
        lock.unlock();
        RequestStatusTablet(deadline);
        return;
      }
    }
    DoAbort(deadline, Status::OK(), transaction);
  }

  bool IsRestartRequired() const {
    return read_point_.IsRestartRequired();
  }

  bool HasOperations() {
    std::lock_guard<std::mutex> lock(mutex_);
    return !tablets_.empty();
  }

  std::shared_future<TransactionMetadata> TEST_GetMetadata() {
    std::unique_lock<std::mutex> lock(mutex_);
    if (metadata_future_.valid()) {
      return metadata_future_;
    }
    metadata_future_ = std::shared_future<TransactionMetadata>(metadata_promise_.get_future());
    if (!ready_) {
      auto transaction = transaction_->shared_from_this();
      waiters_.push_back([this, transaction](const Status& status) {
        // OK to crash here, because we are in test
        CHECK_OK(status);
        metadata_promise_.set_value(metadata_);
      });
      lock.unlock();
      RequestStatusTablet(TransactionRpcDeadline());
    }
    metadata_promise_.set_value(metadata_);
    return metadata_future_;
  }

  void PrepareChild(
      ForceConsistentRead force_consistent_read, CoarseTimePoint deadline,
      PrepareChildCallback callback) {
    auto transaction = transaction_->shared_from_this();
    std::unique_lock<std::mutex> lock(mutex_);
    auto status = CheckRunning(&lock);
    if (!status.ok()) {
      callback(status);
      return;
    }
    if (IsRestartRequired()) {
      lock.unlock();
      callback(STATUS(IllegalState, "Restart required"));
      return;
    }

    SetReadTimeIfNeeded(force_consistent_read);

    if (!ready_) {
      waiters_.emplace_back(std::bind(
          &Impl::DoPrepareChild, this, _1, transaction, std::move(callback), nullptr /* lock */));
      lock.unlock();
      RequestStatusTablet(deadline);
      return;
    }

    DoPrepareChild(Status::OK(), transaction, std::move(callback), &lock);
  }

  Result<ChildTransactionResultPB> FinishChild() {
    std::unique_lock<std::mutex> lock(mutex_);
    RETURN_NOT_OK(CheckRunning(&lock));
    if (!child_) {
      return STATUS(IllegalState, "Finish child of non child transaction");
    }
    state_.store(TransactionState::kCommitted, std::memory_order_release);
    ChildTransactionResultPB result;
    auto& tablets = *result.mutable_tablets();
    tablets.Reserve(tablets_.size());
    for (const auto& tablet : tablets_) {
      auto& out = *tablets.Add();
      out.set_tablet_id(tablet.first);
      tablet.second.ToPB(&out);
    }
    read_point_.FinishChildTransactionResult(HadReadTime(child_had_read_time_), &result);
    return result;
  }

  Status ApplyChildResult(const ChildTransactionResultPB& result) {
    std::unique_lock<std::mutex> lock(mutex_);
    RETURN_NOT_OK(CheckRunning(&lock));
    if (child_) {
      return STATUS(IllegalState, "Apply child result of child transaction");
    }

    for (const auto& tablet : result.tablets()) {
      tablets_[tablet.tablet_id()].MergeFromPB(tablet);
    }
    read_point_.ApplyChildTransactionResult(result);

    return Status::OK();
  }

  const std::string& LogPrefix() {
    return log_prefix_;
  }

  std::string ToString() {
    std::lock_guard<std::mutex> lock(mutex_);
    return Format("{ metadata: $0 state: $1 }", metadata_, state_.load(std::memory_order_acquire));
  }

  const TransactionId& id() const {
    return metadata_.transaction_id;
  }

  ConsistentReadPoint& read_point() {
    return read_point_;
  }

 private:
  void CompleteConstruction() {
    log_prefix_ = Format("$0: ", to_string(metadata_.transaction_id));
    heartbeat_handle_ = manager_->rpcs().InvalidHandle();
    commit_handle_ = manager_->rpcs().InvalidHandle();
    abort_handle_ = manager_->rpcs().InvalidHandle();
  }

  void SetReadTimeIfNeeded(bool do_it) {
    if (!read_point_.GetReadTime() && do_it &&
        metadata_.isolation == IsolationLevel::SNAPSHOT_ISOLATION) {
      read_point_.SetCurrentReadTime();
    }
  }

  bool HasTabletsWithIntents() {
    for (const auto& tablet : tablets_) {
      if (tablet.second.HasMetadata()) {
        return true;
      }
    }
    return false;
  }

  CHECKED_STATUS CheckRunning(std::unique_lock<std::mutex>* lock) {
    if (state_.load(std::memory_order_acquire) != TransactionState::kRunning) {
      auto status = error_;
      lock->unlock();
      if (status.ok()) {
        status = STATUS(IllegalState, "Transaction already completed");
      }
      return status;
    }
    return Status::OK();
  }

  void DoCommit(
      CoarseTimePoint deadline, const Status& status, const YBTransactionPtr& transaction) {
    VLOG_WITH_PREFIX(1) << Format("Commit, tablets: $0, status: $1", tablets_, status);

    if (!status.ok()) {
      commit_callback_(status);
      return;
    }

    // If we don't have any tablets that have intents written to them, just abort it.
    // But notify caller that commit was successful, so it is transparent for him.
    if (!HasTabletsWithIntents()) {
      DoAbort(deadline, Status::OK(), transaction);
      commit_callback_(Status::OK());
      return;
    }

    tserver::UpdateTransactionRequestPB req;
    req.set_tablet_id(status_tablet_->tablet_id());
    req.set_propagated_hybrid_time(manager_->Now().ToUint64());
    auto& state = *req.mutable_state();
    state.set_transaction_id(metadata_.transaction_id.begin(), metadata_.transaction_id.size());
    state.set_status(TransactionStatus::COMMITTED);
    for (const auto& tablet : tablets_) {
      // If metadata is missing then the tablet does not contain intents, so does not
      // need to be involved in Apply/Cleanup.
      if (tablet.second.HasMetadata()) {
        state.add_tablets(tablet.first);
      }
    }

    manager_->rpcs().RegisterAndStart(
        UpdateTransaction(
            deadline,
            status_tablet_.get(),
            manager_->client(),
            &req,
            std::bind(&Impl::CommitDone, this, _1, _2, transaction)),
        &commit_handle_);
  }

  void DoAbort(
      CoarseTimePoint deadline, const Status& status, const YBTransactionPtr& transaction) {
    VLOG_WITH_PREFIX(1) << Format("Abort, status: $1", status);

    if (!status.ok()) {
      // We already stopped to send heartbeats, so transaction would be aborted anyway.
      LOG(WARNING) << "Failed to abort transaction: " << status;
      return;
    }

    tserver::AbortTransactionRequestPB req;
    req.set_tablet_id(status_tablet_->tablet_id());
    req.set_propagated_hybrid_time(manager_->Now().ToUint64());
    req.set_transaction_id(metadata_.transaction_id.begin(), metadata_.transaction_id.size());

    manager_->rpcs().RegisterAndStart(
        AbortTransaction(
            deadline,
            status_tablet_.get(),
            manager_->client(),
            &req,
            std::bind(&Impl::AbortDone, this, _1, _2, transaction)),
        &abort_handle_);

    DoAbortCleanup(transaction);
  }

  void DoAbortCleanup(const YBTransactionPtr& transaction) {
    if (FLAGS_transaction_disable_proactive_cleanup_in_tests) {
      return;
    }

    VLOG_WITH_PREFIX(1) << "Cleaning up intents for " << metadata_.transaction_id;

    std::vector<std::string> tablet_ids;
    {
      std::unique_lock<std::mutex> lock(mutex_);
      tablet_ids.reserve(tablets_.size());
      for (const auto& tablet : tablets_) {
        // If metadata is missing then the tablet does not contain intents, so does not
        // need to be involved in Abort/Cleanup.
        if (tablet.second.HasMetadata()) {
          tablet_ids.push_back(tablet.first);
        }
      }
    }

    for (const auto& tablet_id : tablet_ids) {
      manager_->client()->LookupTabletById(
          tablet_id,
          TransactionRpcDeadline(),
          std::bind(&Impl::LookupTabletForCleanupDone, this, _1, transaction),
          client::UseCache::kTrue);
    }
  }

  void LookupTabletForCleanupDone(const Result<internal::RemoteTabletPtr>& remote_tablet,
                                  const YBTransactionPtr& transaction) {
    if (!remote_tablet.ok()) {
      // Intents will be cleaned up later in this case.
      LOG(WARNING) << "Tablet lookup failed: " << remote_tablet.status();
      return;
    }
    VLOG_WITH_PREFIX(1) << "Lookup tablet for cleanup done: " << yb::ToString(*remote_tablet);
    auto remote_tablet_servers = (**remote_tablet).GetRemoteTabletServers(
        internal::IncludeFailedReplicas::kTrue);

    constexpr auto kCallTimeout = 15s;
    auto now = manager_->Now().ToUint64();

    {
      std::unique_lock<std::mutex> lock(mutex_);
      abort_requests_.reserve(abort_requests_.size() + remote_tablet_servers.size());
      for (auto* server : remote_tablet_servers) {
        VLOG_WITH_PREFIX(2) << "Sending cleanup to: " << yb::ToString(*server);
        auto status = server->InitProxy(manager_->client());
        if (!status.ok()) {
          LOG(WARNING) << "Failed to init proxy to " << server->ToString() << ": " << status;
          continue;
        }
        abort_requests_.emplace_back();
        auto& abort_request = abort_requests_.back();

        auto& request = abort_request.request;
        request.set_tablet_id((**remote_tablet).tablet_id());
        request.set_propagated_hybrid_time(now);
        auto& state = *request.mutable_state();
        state.set_transaction_id(metadata_.transaction_id.begin(), metadata_.transaction_id.size());
        state.set_status(TransactionStatus::CLEANUP);

        abort_request.controller.set_timeout(kCallTimeout);

        server->proxy()->UpdateTransactionAsync(
            request, &abort_request.response, &abort_request.controller,
            std::bind(&Impl::ProcessResponse, this, transaction));
      }
    }
  }

  void ProcessResponse(const YBTransactionPtr& transaction) {
    VLOG_WITH_PREFIX(3) << "Cleanup intents for Abort done";
  }

  void CommitDone(const Status& status,
                  HybridTime propagated_hybrid_time,
                  const YBTransactionPtr& transaction) {
    VLOG_WITH_PREFIX(1) << "Committed: " << status;

    manager_->UpdateClock(propagated_hybrid_time);
    manager_->rpcs().Unregister(&commit_handle_);
    commit_callback_(status.IsAlreadyPresent() ? Status::OK() : status);
  }

  void AbortDone(const Status& status,
                 const tserver::AbortTransactionResponsePB& response,
                 const YBTransactionPtr& transaction) {
    VLOG_WITH_PREFIX(1) << "Aborted: " << status;

    if (response.has_propagated_hybrid_time()) {
      manager_->UpdateClock(HybridTime(response.propagated_hybrid_time()));
    }
    manager_->rpcs().Unregister(&abort_handle_);
  }

  void RequestStatusTablet(const CoarseTimePoint& deadline) {
    bool expected = false;
    if (!requested_status_tablet_.compare_exchange_strong(
        expected, true, std::memory_order_acq_rel)) {
      return;
    }
    manager_->PickStatusTablet(
        std::bind(&Impl::StatusTabletPicked, this, _1, deadline, transaction_->shared_from_this()));
  }

  void StatusTabletPicked(const Result<std::string>& tablet,
                          const CoarseTimePoint& deadline,
                          const YBTransactionPtr& transaction) {
    VLOG_WITH_PREFIX(2) << "Picked status tablet: " << tablet;

    if (!tablet.ok()) {
      NotifyWaiters(tablet.status());
      return;
    }

    manager_->client()->LookupTabletById(
        *tablet,
        deadline,
        std::bind(&Impl::LookupTabletDone, this, _1, transaction),
        client::UseCache::kTrue);
  }

  void LookupTabletDone(const Result<client::internal::RemoteTabletPtr>& result,
                        const YBTransactionPtr& transaction) {
    VLOG_WITH_PREFIX(1) << "Lookup tablet done: " << yb::ToString(result);

    if (!result.ok()) {
      NotifyWaiters(result.status());
      return;
    }

    {
      std::lock_guard<std::mutex> lock(mutex_);
      status_tablet_ = std::move(*result);
      metadata_.status_tablet = status_tablet_->tablet_id();
    }
    SendHeartbeat(TransactionStatus::CREATED, metadata_.transaction_id,
                  transaction_->shared_from_this());
  }

  void NotifyWaiters(const Status& status) {
    std::vector<Waiter> waiters;
    {
      std::lock_guard<std::mutex> lock(mutex_);
      SetError(status, &lock);
      waiters_.swap(waiters);
    }
    for (const auto& waiter : waiters) {
      waiter(status);
    }
  }

  void SendHeartbeat(TransactionStatus status,
                     const TransactionId& id,
                     std::weak_ptr<YBTransaction> weak_transaction) {
    auto transaction = weak_transaction.lock();
    if (!transaction || state_.load(std::memory_order_acquire) != TransactionState::kRunning) {
      VLOG(1) << id << " Send heartbeat cancelled: " << yb::ToString(transaction);
      return;
    }

    if (status != TransactionStatus::CREATED &&
        GetAtomicFlag(&FLAGS_transaction_disable_heartbeat_in_tests)) {
      HeartbeatDone(Status::OK(), HybridTime::kInvalid, status, transaction);
      return;
    }

    tserver::UpdateTransactionRequestPB req;
    req.set_tablet_id(status_tablet_->tablet_id());
    req.set_propagated_hybrid_time(manager_->Now().ToUint64());
    auto& state = *req.mutable_state();
    state.set_transaction_id(metadata_.transaction_id.begin(), metadata_.transaction_id.size());
    state.set_status(status);
    manager_->rpcs().RegisterAndStart(
        UpdateTransaction(
            TransactionRpcDeadline(),
            status_tablet_.get(),
            manager_->client(),
            &req,
            std::bind(&Impl::HeartbeatDone, this, _1, _2, status, transaction)),
        &heartbeat_handle_);
  }

  void HeartbeatDone(const Status& status,
                     HybridTime propagated_hybrid_time,
                     TransactionStatus transaction_status,
                     const YBTransactionPtr& transaction) {
    manager_->UpdateClock(propagated_hybrid_time);
    manager_->rpcs().Unregister(&heartbeat_handle_);

    if (status.ok()) {
      if (transaction_status == TransactionStatus::CREATED) {
        std::vector<Waiter> waiters;
        {
          std::lock_guard<std::mutex> lock(mutex_);
          DCHECK(!ready_);
          ready_ = true;
          waiters_.swap(waiters);
        }
        VLOG_WITH_PREFIX(1) << "Created, notifying waiters: " << waiters.size();
        for (const auto& waiter : waiters) {
          waiter(Status::OK());
        }
      }
      std::weak_ptr<YBTransaction> weak_transaction(transaction);
      manager_->client()->messenger()->scheduler().Schedule(
          [this, weak_transaction](const Status&) {
              SendHeartbeat(TransactionStatus::PENDING, metadata_.transaction_id, weak_transaction);
          },
          std::chrono::microseconds(FLAGS_transaction_heartbeat_usec));
    } else {
      LOG_WITH_PREFIX(WARNING) << "Send heartbeat failed: " << status;
      if (status.IsAborted()) {
        // Service is shutting down, no reason to retry.
        SetError(status);
        return;
      } else if (status.IsExpired()) {
        SetError(status);
        // If state is committed, then we should not cleanup.
        if (state_.load(std::memory_order_acquire) == TransactionState::kRunning) {
          DoAbortCleanup(transaction);
        }
        return;
      }
      // Other errors could have different causes, but we should just retry sending heartbeat
      // in this case.
      SendHeartbeat(transaction_status, metadata_.transaction_id, transaction);
    }
  }

  void SetError(const Status& status, std::lock_guard<std::mutex>* lock = nullptr) {
    VLOG_WITH_PREFIX(1) << "Failed: " << status;
    if (!lock) {
      std::lock_guard<std::mutex> new_lock(mutex_);
      SetError(status, &new_lock);
      return;
    }
    if (error_.ok()) {
      error_ = status;
      state_.store(TransactionState::kAborted, std::memory_order_release);
    }
  }

  void DoPrepareChild(const Status& status,
                      const YBTransactionPtr& transaction,
                      PrepareChildCallback callback,
                      std::unique_lock<std::mutex>* parent_lock) {
    if (!status.ok()) {
      callback(status);
      return;
    }

    std::unique_lock<std::mutex> lock(mutex_, std::defer_lock);
    if (!parent_lock) {
      lock.lock();
    }
    ChildTransactionDataPB data;
    metadata_.ToPB(data.mutable_metadata());
    read_point_.PrepareChildTransactionData(&data);
    callback(data);
  }

  // Manager is created once per service.
  TransactionManager* const manager_;

  // Transaction related to this impl.
  YBTransaction* const transaction_;

  TransactionMetadata metadata_;
  ConsistentReadPoint read_point_;

  std::string log_prefix_;
  std::atomic<bool> requested_status_tablet_{false};
  internal::RemoteTabletPtr status_tablet_;
  std::atomic<TransactionState> state_{TransactionState::kRunning};
  // Transaction is successfully initialized and ready to process intents.
  const bool child_;
  bool child_had_read_time_ = false;
  bool ready_ = false;
  CommitCallback commit_callback_;
  Status error_;
  rpc::Rpcs::Handle heartbeat_handle_;
  rpc::Rpcs::Handle commit_handle_;
  rpc::Rpcs::Handle abort_handle_;

  // RPC data for abort requests.
  struct AbortRequest {
    tserver::UpdateTransactionRequestPB request;
    tserver::UpdateTransactionResponsePB response;
    rpc::RpcController controller;
  };

  boost::container::stable_vector<AbortRequest> abort_requests_;

  struct TabletState {
    InvolvedTabletMetadataState metadata_state = InvolvedTabletMetadataState::MISSING;

    void ToPB(TransactionInvolvedTabletPB* out) const {
      out->set_metadata_state(metadata_state);
    }

    void MergeFromPB(const TransactionInvolvedTabletPB& source) {
      switch (source.metadata_state()) {
        case InvolvedTabletMetadataState::MISSING:
          break;
        case InvolvedTabletMetadataState::EXIST:
          metadata_state = InvolvedTabletMetadataState::EXIST;
          break;
        case InvolvedTabletMetadataState::MAY_EXIST:
          if (metadata_state == InvolvedTabletMetadataState::MISSING) {
            metadata_state = InvolvedTabletMetadataState::MAY_EXIST;
          }
          break;
      }
    }

    bool HasMetadata() const {
      return true;
    }

    std::string ToString() const {
      return Format("{ metadata_state $0 }", InvolvedTabletMetadataState_Name(metadata_state));
    }
  };

  typedef std::unordered_map<std::string, TabletState> TabletStates;

  std::mutex mutex_;
  TabletStates tablets_;
  std::vector<Waiter> waiters_;
  std::promise<TransactionMetadata> metadata_promise_;
  std::shared_future<TransactionMetadata> metadata_future_;
};

CoarseTimePoint AdjustDeadline(CoarseTimePoint deadline) {
  if (deadline == CoarseTimePoint()) {
    return TransactionRpcDeadline();
  }
  return deadline;
}

YBTransaction::YBTransaction(TransactionManager* manager)
    : impl_(new Impl(manager, this)) {
}

YBTransaction::YBTransaction(TransactionManager* manager, ChildTransactionData data)
    : impl_(new Impl(manager, this, std::move(data))) {
}

YBTransaction::~YBTransaction() {
}

Status YBTransaction::Init(IsolationLevel isolation, const ReadHybridTime& read_time) {
  return impl_->Init(isolation, read_time);
}

void YBTransaction::InitWithReadPoint(
    IsolationLevel isolation,
    ConsistentReadPoint&& read_point) {
  return impl_->InitWithReadPoint(isolation, std::move(read_point));
}

bool YBTransaction::Prepare(const internal::InFlightOps& ops,
                            ForceConsistentRead force_consistent_read,
                            CoarseTimePoint deadline,
                            Waiter waiter,
                            TransactionMetadata* metadata) {
  return impl_->Prepare(
      ops, force_consistent_read, deadline, std::move(waiter), metadata);
}

void YBTransaction::Flushed(
    const internal::InFlightOps& ops, const ReadHybridTime& used_read_time, const Status& status) {
  impl_->Flushed(ops, used_read_time, status);
}

void YBTransaction::Commit(CoarseTimePoint deadline, CommitCallback callback) {
  impl_->Commit(AdjustDeadline(deadline), std::move(callback));
}

const TransactionId& YBTransaction::id() const {
  return impl_->id();
}

const IsolationLevel YBTransaction::isolation() const {
  return impl_->isolation();
}

const ConsistentReadPoint& YBTransaction::read_point() const {
  return impl_->read_point();
}

ConsistentReadPoint& YBTransaction::read_point() {
  return impl_->read_point();
}

std::future<Status> YBTransaction::CommitFuture(CoarseTimePoint deadline) {
  return MakeFuture<Status>([this, deadline](auto callback) {
    impl_->Commit(AdjustDeadline(deadline), std::move(callback));
  });
}

void YBTransaction::Abort(CoarseTimePoint deadline) {
  impl_->Abort(AdjustDeadline(deadline));
}

bool YBTransaction::IsRestartRequired() const {
  return impl_->IsRestartRequired();
}

bool YBTransaction::HasOperations() const {
  return impl_->HasOperations();
}

Result<YBTransactionPtr> YBTransaction::CreateRestartedTransaction() {
  auto result = impl_->CreateSimilarTransaction();
  RETURN_NOT_OK(impl_->FillRestartedTransaction(result->impl_.get()));
  return result;
}

Status YBTransaction::FillRestartedTransaction(const YBTransactionPtr& dest) {
  return impl_->FillRestartedTransaction(dest->impl_.get());
}

void YBTransaction::PrepareChild(
    ForceConsistentRead force_consistent_read, CoarseTimePoint deadline,
    PrepareChildCallback callback) {
  return impl_->PrepareChild(force_consistent_read, deadline, std::move(callback));
}

std::future<Result<ChildTransactionDataPB>> YBTransaction::PrepareChildFuture(
    ForceConsistentRead force_consistent_read, CoarseTimePoint deadline) {
  return MakeFuture<Result<ChildTransactionDataPB>>(
      [this, deadline, force_consistent_read](auto callback) {
    impl_->PrepareChild(force_consistent_read, AdjustDeadline(deadline), std::move(callback));
  });
}

Result<ChildTransactionResultPB> YBTransaction::FinishChild() {
  return impl_->FinishChild();
}

std::shared_future<TransactionMetadata> YBTransaction::TEST_GetMetadata() const {
  return impl_->TEST_GetMetadata();
}

Status YBTransaction::ApplyChildResult(const ChildTransactionResultPB& result) {
  return impl_->ApplyChildResult(result);
}

std::string YBTransaction::ToString() const {
  return impl_->ToString();
}

} // namespace client
} // namespace yb
