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

#include "yb/tserver/service_util.h"

#include "yb/common/wire_protocol.h"

#include "yb/consensus/consensus.h"
#include "yb/consensus/consensus_error.h"
#include "yb/consensus/raft_consensus.h"

#include "yb/tablet/tablet.h"
#include "yb/tablet/tablet_metadata.h"
#include "yb/tablet/tablet_metrics.h"

#include "yb/tserver/tserver_error.h"

#include "yb/util/flag_tags.h"
#include "yb/util/mem_tracker.h"
#include "yb/util/metrics.h"

DEFINE_test_flag(bool, assert_reads_from_follower_rejected_because_of_staleness, false,
                 "If set, we verify that the consistency level is CONSISTENT_PREFIX, and that "
                 "a follower receives the request, but that it gets rejected because it's a stale "
                 "follower");

DEFINE_uint64(
    max_stale_read_bound_time_ms, 60000,
    "If we are allowed to read from followers, specify the maximum time a follower can be behind "
    "by using the last message received from the leader. If set to zero, a read can be served by a "
    "follower regardless of when was the last time it received a message from the leader or how "
    "far behind this follower is.");
TAG_FLAG(max_stale_read_bound_time_ms, evolving);
TAG_FLAG(max_stale_read_bound_time_ms, runtime);

DEFINE_uint64(sst_files_soft_limit, 24,
              "When majority SST files number is greater that this limit, we will start rejecting "
              "part of write requests. The higher the number of SST files, the higher probability "
              "of rejection.");
TAG_FLAG(sst_files_soft_limit, runtime);

DEFINE_uint64(sst_files_hard_limit, 48,
              "When majority SST files number is greater that this limit, we will reject all write "
              "requests.");
TAG_FLAG(sst_files_hard_limit, runtime);

DEFINE_test_flag(int32, write_rejection_percentage, 0,
                 "Reject specified percentage of writes.");

DEFINE_uint64(min_rejection_delay_ms, 100,
              "Minimal delay for rejected write to be retried in milliseconds.");
TAG_FLAG(min_rejection_delay_ms, runtime);

DEFINE_uint64(max_rejection_delay_ms, 5000, ""
              "Maximal delay for rejected write to be retried in milliseconds.");
TAG_FLAG(max_rejection_delay_ms, runtime);

DECLARE_int32(memory_limit_warn_threshold_percentage);

namespace yb {
namespace tserver {

void SetupErrorAndRespond(TabletServerErrorPB* error,
                          const Status& s,
                          TabletServerErrorPB::Code code,
                          rpc::RpcContext* context) {
  // Generic "service unavailable" errors will cause the client to retry later.
  if (code == TabletServerErrorPB::UNKNOWN_ERROR) {
    if (s.IsServiceUnavailable()) {
      TabletServerDelay delay(s);
      if (!delay.value().Initialized()) {
        context->RespondRpcFailure(rpc::ErrorStatusPB::ERROR_SERVER_TOO_BUSY, s);
        return;
      }
    }
    consensus::ConsensusError consensus_error(s);
    if (consensus_error.value() == consensus::ConsensusErrorPB::TABLET_SPLIT) {
      code = TabletServerErrorPB::TABLET_SPLIT;
    }
  }

  StatusToPB(s, error->mutable_status());
  error->set_code(code);
  context->RespondSuccess();
}

void SetupErrorAndRespond(TabletServerErrorPB* error,
                          const Status& s,
                          rpc::RpcContext* context) {
  auto ts_error = TabletServerError::FromStatus(s);
  SetupErrorAndRespond(
      error, s, ts_error ? ts_error->value() : TabletServerErrorPB::UNKNOWN_ERROR, context);
}

void SetupError(TabletServerErrorPB* error, const Status& s) {
  auto ts_error = TabletServerError::FromStatus(s);
  auto code = ts_error ? ts_error->value() : TabletServerErrorPB::UNKNOWN_ERROR;
  if (code == TabletServerErrorPB::UNKNOWN_ERROR) {
    consensus::ConsensusError consensus_error(s);
    if (consensus_error.value() == consensus::ConsensusErrorPB::TABLET_SPLIT) {
      code = TabletServerErrorPB::TABLET_SPLIT;
    }
  }
  StatusToPB(s, error->mutable_status());
  error->set_code(code);
}

Result<int64_t> LeaderTerm(const tablet::TabletPeer& tablet_peer) {
  std::shared_ptr<consensus::Consensus> consensus = tablet_peer.shared_consensus();
  if (!consensus) {
    auto state = tablet_peer.state();
    if (state != tablet::RaftGroupStatePB::SHUTDOWN) {
      // Should not happen.
      return STATUS(IllegalState, "Tablet peer does not have consensus, but in $0 state",
                    tablet::RaftGroupStatePB_Name(state));
    }
    return STATUS(Aborted, "Tablet peer was closed");
  }
  auto leader_state = consensus->GetLeaderState();

  VLOG(1) << Format(
      "Check for tablet $0 peer $1. Peer role is $2. Leader status is $3.",
      tablet_peer.tablet_id(), tablet_peer.permanent_uuid(),
      consensus->role(), to_underlying(leader_state.status));

  if (!leader_state.ok()) {
    typedef consensus::LeaderStatus LeaderStatus;
    auto status = leader_state.CreateStatus();
    switch (leader_state.status) {
      case LeaderStatus::NOT_LEADER: FALLTHROUGH_INTENDED;
      case LeaderStatus::LEADER_BUT_NO_MAJORITY_REPLICATED_LEASE:
        // We are returning a NotTheLeader as opposed to LeaderNotReady, because there is a chance
        // that we're a partitioned-away leader, and the client needs to do another leader lookup.
        return status.CloneAndAddErrorCode(TabletServerError(TabletServerErrorPB::NOT_THE_LEADER));
      case LeaderStatus::LEADER_BUT_NO_OP_NOT_COMMITTED: FALLTHROUGH_INTENDED;
      case LeaderStatus::LEADER_BUT_OLD_LEADER_MAY_HAVE_LEASE:
        return status.CloneAndAddErrorCode(TabletServerError(
            TabletServerErrorPB::LEADER_NOT_READY_TO_SERVE));
      case LeaderStatus::LEADER_AND_READY:
        LOG(FATAL) << "Unexpected status: " << to_underlying(leader_state.status);
    }
    FATAL_INVALID_ENUM_VALUE(LeaderStatus, leader_state.status);
  }

  return leader_state.term;
}

void LeaderTabletPeer::FillTabletPeer(TabletPeerTablet source) {
  peer = std::move(source.tablet_peer);
  tablet = std::move(source.tablet);
}

Status LeaderTabletPeer::FillTerm() {
  auto leader_term_result = LeaderTerm(*peer);
  if (!leader_term_result.ok()) {
    auto tablet = peer->shared_tablet();
    if (tablet) {
      // It could happen that tablet becomes nullptr due to shutdown.
      tablet->metrics()->not_leader_rejections->Increment();
    }
    return leader_term_result.status();
  }
  leader_term = *leader_term_result;

  return Status::OK();
}

Result<LeaderTabletPeer> LookupLeaderTablet(
    TabletPeerLookupIf* tablet_manager,
    const std::string& tablet_id,
    TabletPeerTablet peer) {
  if (peer.tablet_peer) {
    LOG_IF(DFATAL, peer.tablet_peer->tablet_id() != tablet_id)
        << "Mismatching table ids: peer " << peer.tablet_peer->tablet_id()
        << " vs " << tablet_id;
    LOG_IF(DFATAL, !peer.tablet) << "Empty tablet pointer for tablet id : " << tablet_id;
  } else {
    peer = VERIFY_RESULT(LookupTabletPeer(tablet_manager, tablet_id));
  }
  LeaderTabletPeer result;
  result.FillTabletPeer(std::move(peer));

  RETURN_NOT_OK(result.FillTerm());
  return result;
}

Status CheckPeerIsReady(
    const tablet::TabletPeer& tablet_peer, AllowSplitTablet allow_split_tablet) {
  auto consensus = tablet_peer.shared_consensus();
  if (!consensus) {
    return STATUS(
        IllegalState, Format("Consensus not available for tablet $0.", tablet_peer.tablet_id()),
        Slice(), TabletServerError(TabletServerErrorPB::TABLET_NOT_RUNNING));
  }

  Status s = tablet_peer.CheckRunning();
  if (!s.ok()) {
    return s.CloneAndAddErrorCode(TabletServerError(TabletServerErrorPB::TABLET_NOT_RUNNING));
  }

  auto* tablet = tablet_peer.tablet();
  SCHECK(tablet != nullptr, IllegalState, "Expected tablet peer to have a tablet");
  const auto tablet_data_state = tablet->metadata()->tablet_data_state();
  if (!allow_split_tablet &&
      tablet_data_state == tablet::TabletDataState::TABLET_DATA_SPLIT_COMPLETED) {
    auto split_child_tablet_ids = tablet->metadata()->split_child_tablet_ids();
    return STATUS(
               IllegalState,
               Format("The tablet $0 is in $1 state",
                      tablet->tablet_id(),
                      TabletDataState_Name(tablet_data_state)),
               TabletServerError(TabletServerErrorPB::TABLET_SPLIT))
        .CloneAndAddErrorCode(SplitChildTabletIdsData(
            std::vector<TabletId>(split_child_tablet_ids.begin(), split_child_tablet_ids.end())));
    // TODO(tsplit): If we get FS corruption on 1 node, we can just delete that tablet copy and
    // bootstrap from a good leader. If there's a way that all peers replicated the SPLIT and
    // modified their data state, but all had some failures (code bug?).
    // Perhaps we should consider a tool for editing the data state?
  }
  return Status::OK();
}


Status CheckPeerIsLeader(const tablet::TabletPeer& tablet_peer) {
  return ResultToStatus(LeaderTerm(tablet_peer));
}

Result<TabletPeerTablet> LookupTabletPeer(
    TabletPeerLookupIf* tablet_manager,
    const TabletId& tablet_id) {
  TabletPeerTablet result;
  auto status = tablet_manager->GetTabletPeer(tablet_id, &result.tablet_peer);
  if (PREDICT_FALSE(!status.ok())) {
    auto code = status.IsServiceUnavailable() ? TabletServerErrorPB::UNKNOWN_ERROR
                                              : TabletServerErrorPB::TABLET_NOT_FOUND;
    return status.CloneAndAddErrorCode(TabletServerError(code));
  }

  // Check RUNNING state.
  tablet::RaftGroupStatePB state = result.tablet_peer->state();
  if (PREDICT_FALSE(state != tablet::RUNNING)) {
    Status s = STATUS(IllegalState,  Format("Tablet $0 not RUNNING", tablet_id),
                      tablet::RaftGroupStateError(state))
        .CloneAndAddErrorCode(TabletServerError(TabletServerErrorPB::TABLET_NOT_RUNNING));
    return s;
  }

  result.tablet = result.tablet_peer->shared_tablet();
  if (!result.tablet) {
    Status s = STATUS(IllegalState,
                      "Tablet not running",
                      TabletServerError(TabletServerErrorPB::TABLET_NOT_RUNNING));
    return s;
  }
  return result;
}

Result<std::shared_ptr<tablet::AbstractTablet>> GetTablet(
    TabletPeerLookupIf* tablet_manager, const TabletId& tablet_id,
    tablet::TabletPeerPtr tablet_peer, YBConsistencyLevel consistency_level,
    AllowSplitTablet allow_split_tablet) {
  tablet::TabletPtr tablet_ptr = nullptr;
  if (tablet_peer) {
    DCHECK_EQ(tablet_peer->tablet_id(), tablet_id);
    tablet_ptr = tablet_peer->shared_tablet();
    LOG_IF(DFATAL, tablet_ptr == nullptr)
        << "Empty tablet pointer for tablet id: " << tablet_id;
  } else {
    auto tablet_peer_result = VERIFY_RESULT(LookupTabletPeer(tablet_manager, tablet_id));

    tablet_peer = std::move(tablet_peer_result.tablet_peer);
    tablet_ptr = std::move(tablet_peer_result.tablet);
  }

  RETURN_NOT_OK(CheckPeerIsReady(*tablet_peer, allow_split_tablet));

  // Check for leader only in strong consistency level.
  if (consistency_level == YBConsistencyLevel::STRONG) {
    if (PREDICT_FALSE(FLAGS_TEST_assert_reads_from_follower_rejected_because_of_staleness)) {
      LOG(FATAL) << "--TEST_assert_reads_from_follower_rejected_because_of_staleness is true but "
                    "consistency level is invalid: YBConsistencyLevel::STRONG";
    }

    RETURN_NOT_OK(CheckPeerIsLeader(*tablet_peer));
  } else {
    auto s = CheckPeerIsLeader(*tablet_peer.get());

    // Peer is not the leader, so check that the time since it last heard from the leader is less
    // than FLAGS_max_stale_read_bound_time_ms.
    if (PREDICT_FALSE(!s.ok())) {
      if (FLAGS_max_stale_read_bound_time_ms > 0) {
        auto consensus = tablet_peer->shared_consensus();
        // TODO(hector): This safe time could be reused by the read operation.
        auto safe_time_micros = tablet_peer->tablet()->mvcc_manager()->SafeTimeForFollower(
            HybridTime::kMin, CoarseTimePoint::min()).GetPhysicalValueMicros();
        auto now_micros = tablet_peer->clock_ptr()->Now().GetPhysicalValueMicros();
        auto follower_staleness_us = now_micros - safe_time_micros;
        if (follower_staleness_us > FLAGS_max_stale_read_bound_time_ms * 1000) {
          VLOG(1) << "Rejecting stale read with staleness "
                     << follower_staleness_us << "us";
          return STATUS(
              IllegalState, "Stale follower",
              TabletServerError(TabletServerErrorPB::STALE_FOLLOWER));
        } else if (PREDICT_FALSE(
            FLAGS_TEST_assert_reads_from_follower_rejected_because_of_staleness)) {
          LOG(FATAL) << "--TEST_assert_reads_from_follower_rejected_because_of_staleness is true,"
                     << " but peer " << tablet_peer->permanent_uuid()
                     << " for tablet: " << tablet_id
                     << " is not stale. Time since last update from leader: "
                     << follower_staleness_us << "us";
        } else {
          VLOG(3) << "Reading from follower with staleness: " << follower_staleness_us << "us";
        }
      }
    } else {
      // We are here because we are the leader.
      if (PREDICT_FALSE(FLAGS_TEST_assert_reads_from_follower_rejected_because_of_staleness)) {
        LOG(FATAL) << "--TEST_assert_reads_from_follower_rejected_because_of_staleness is true but "
                   << " peer " << tablet_peer->permanent_uuid()
                   << " is the leader for tablet " << tablet_id;
      }
    }
  }

  auto tablet = tablet_peer->shared_tablet();
  if (PREDICT_FALSE(!tablet)) {
    return STATUS_EC_FORMAT(
        IllegalState, TabletServerError(TabletServerErrorPB::TABLET_NOT_RUNNING),
        "Tablet $0 is not running", tablet_id);
  }

  return tablet;
}

// overlimit - we have 2 bounds, value and random score.
// overlimit is calculated as:
// score + (value - lower_bound) / (upper_bound - lower_bound).
// And it will be >= 1.0 when this function is invoked.
Status RejectWrite(
    tablet::TabletPeer* tablet_peer, const std::string& message, double overlimit) {
  int64_t delay_ms = fit_bounds<int64_t>((overlimit - 1.0) * FLAGS_max_rejection_delay_ms,
                                         FLAGS_min_rejection_delay_ms,
                                         FLAGS_max_rejection_delay_ms);
  auto status = STATUS(
      ServiceUnavailable, message, TabletServerDelay(std::chrono::milliseconds(delay_ms)));
  YB_LOG_EVERY_N_SECS(WARNING, 1)
      << "T " << tablet_peer->tablet_id() << " P " << tablet_peer->permanent_uuid()
      << ": Rejecting Write request, " << status << THROTTLE_MSG;
  return status;
}

Status CheckWriteThrottling(double score, tablet::TabletPeer* tablet_peer) {
  // Check for memory pressure; don't bother doing any additional work if we've
  // exceeded the limit.
  auto tablet = tablet_peer->tablet();
  auto soft_limit_exceeded_result = tablet->mem_tracker()->AnySoftLimitExceeded(score);
  if (soft_limit_exceeded_result.exceeded) {
    tablet->metrics()->leader_memory_pressure_rejections->Increment();
    string msg = StringPrintf(
        "Soft memory limit exceeded (at %.2f%% of capacity), score: %.2f",
        soft_limit_exceeded_result.current_capacity_pct, score);
    if (soft_limit_exceeded_result.current_capacity_pct >=
            FLAGS_memory_limit_warn_threshold_percentage) {
      YB_LOG_EVERY_N_SECS(WARNING, 1) << "Rejecting Write request: " << msg << THROTTLE_MSG;
    } else {
      YB_LOG_EVERY_N_SECS(INFO, 1) << "Rejecting Write request: " << msg << THROTTLE_MSG;
    }
    return STATUS(ServiceUnavailable, msg);
  }

  const uint64_t num_sst_files = tablet_peer->raft_consensus()->MajorityNumSSTFiles();
  const auto sst_files_soft_limit = FLAGS_sst_files_soft_limit;
  const int64_t sst_files_used_delta = num_sst_files - sst_files_soft_limit;
  if (sst_files_used_delta >= 0) {
    const auto sst_files_hard_limit = FLAGS_sst_files_hard_limit;
    const auto sst_files_full_delta = sst_files_hard_limit - sst_files_soft_limit;
    if (sst_files_used_delta >= sst_files_full_delta * (1 - score)) {
      tablet->metrics()->majority_sst_files_rejections->Increment();
      auto message = Format("SST files limit exceeded $0 against ($1, $2), score: $3",
                            num_sst_files, sst_files_soft_limit, sst_files_hard_limit, score);
      auto overlimit = sst_files_full_delta > 0
          ? score + static_cast<double>(sst_files_used_delta) / sst_files_full_delta
          : 2.0;
      return RejectWrite(tablet_peer, message, overlimit);
    }
  }

  if (FLAGS_TEST_write_rejection_percentage != 0 &&
      score >= 1.0 - FLAGS_TEST_write_rejection_percentage * 0.01) {
    auto status = Format("TEST: Write request rejected, desired percentage: $0, score: $1",
                         FLAGS_TEST_write_rejection_percentage, score);
    return RejectWrite(tablet_peer, status, score + FLAGS_TEST_write_rejection_percentage * 0.01);
  }

  return Status::OK();
}

} // namespace tserver
} // namespace yb
