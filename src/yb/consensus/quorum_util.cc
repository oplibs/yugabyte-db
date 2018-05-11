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
#include "yb/consensus/quorum_util.h"

#include <set>
#include <string>

#include "yb/gutil/map-util.h"
#include "yb/gutil/strings/substitute.h"
#include "yb/util/status.h"

namespace yb {
namespace consensus {

using google::protobuf::RepeatedPtrField;
using std::string;
using strings::Substitute;

bool IsRaftConfigMember(const std::string& uuid, const RaftConfigPB& config) {
  for (const RaftPeerPB& peer : config.peers()) {
    if (peer.permanent_uuid() == uuid) {
      return true;
    }
  }
  return false;
}

bool IsRaftConfigVoter(const std::string& uuid, const RaftConfigPB& config) {
  for (const RaftPeerPB& peer : config.peers()) {
    if (peer.permanent_uuid() == uuid) {
      return peer.member_type() == RaftPeerPB::VOTER;
    }
  }
  return false;
}

Status GetRaftConfigMember(const RaftConfigPB& config,
                           const std::string& uuid,
                           RaftPeerPB* peer_pb) {
  for (const RaftPeerPB& peer : config.peers()) {
    if (peer.permanent_uuid() == uuid) {
      *peer_pb = peer;
      return Status::OK();
    }
  }
  return STATUS(NotFound, Substitute("Peer with uuid $0 not found in consensus config { $1 }",
                                     uuid, config.ShortDebugString()));
}

Status GetMutableRaftConfigMember(RaftConfigPB* config,
                                  const std::string& uuid,
                                  RaftPeerPB** peer_pb) {
  for (int i = 0; i < config->peers_size(); ++i) {
    auto peer = config->mutable_peers(i);
    if (peer->permanent_uuid() == uuid) {
      *peer_pb = peer;
      return Status::OK();
    }
  }
  return STATUS(NotFound, Substitute("Peer with uuid $0 not found in consensus config", uuid));
}

Status GetRaftConfigLeader(const ConsensusStatePB& cstate, RaftPeerPB* peer_pb) {
  if (!cstate.has_leader_uuid() || cstate.leader_uuid().empty()) {
    return STATUS(NotFound, "Consensus config has no leader");
  }
  return GetRaftConfigMember(cstate.config(), cstate.leader_uuid(), peer_pb);
}

Status GetHostPortFromConfig(const RaftConfigPB& config, const std::string& uuid, HostPort* hp) {
  if (!hp) {
    return STATUS(InvalidArgument, "Need a non-null hostport.");
  }
  for (const RaftPeerPB& peer : config.peers()) {
    if (peer.permanent_uuid() == uuid) {
      *hp = HostPortFromPB(peer.last_known_addr());
      return Status::OK();
    }
  }
  return STATUS(NotFound, Substitute("Consensus config did not find $0.", uuid));
}

bool RemoveFromRaftConfig(RaftConfigPB* config, const ChangeConfigRequestPB& req) {
  RepeatedPtrField<RaftPeerPB> modified_peers;
  bool removed = false;
  bool use_host = req.has_use_host() && req.use_host();
  const HostPortPB hp = req.server().last_known_addr();
  if (use_host) {
    LOG(INFO) << "Using host/port " << hp.ShortDebugString() << " instead of UUID";
  }
  const string& uuid = req.server().permanent_uuid();
  for (const RaftPeerPB& peer : config->peers()) {
    if ((use_host && peer.last_known_addr().host() == hp.host() &&
         peer.last_known_addr().port() == hp.port()) ||
        (!use_host && peer.permanent_uuid() == uuid)) {
      removed = true;
      continue;
    }
    *modified_peers.Add() = peer;
  }
  if (!removed) return false;
  config->mutable_peers()->Swap(&modified_peers);
  return true;
}

int CountVoters(const RaftConfigPB& config) {
  return CountMemberType(config, RaftPeerPB::VOTER);
}

int CountVotersInTransition(const RaftConfigPB& config) {
  return CountMemberType(config, RaftPeerPB::PRE_VOTER);
}

int CountServersInTransition(const RaftConfigPB& config, const string& ignore_uuid) {
  return CountMemberType(config, RaftPeerPB::PRE_VOTER, ignore_uuid) +
      CountMemberType(config, RaftPeerPB::PRE_OBSERVER, ignore_uuid);
}

int CountMemberType(const RaftConfigPB& config, const RaftPeerPB::MemberType member_type,
                    const string& ignore_uuid) {
  int count = 0;
  for (const RaftPeerPB& peer : config.peers()) {
    if (peer.member_type() == member_type && peer.permanent_uuid() != ignore_uuid) {
      count++;
    }
  }
  return count;
}

int MajoritySize(int num_voters) {
  DCHECK_GE(num_voters, 1);
  return (num_voters / 2) + 1;
}

RaftPeerPB::MemberType GetConsensusMemberType(const std::string& permanent_uuid,
                                              const ConsensusStatePB& cstate) {
  for (const RaftPeerPB& peer : cstate.config().peers()) {
    if (peer.permanent_uuid() == permanent_uuid) {
      return peer.member_type();
    }
  }
  return RaftPeerPB::UNKNOWN_MEMBER_TYPE;
}

RaftPeerPB::Role GetConsensusRole(const std::string& permanent_uuid,
                                  const ConsensusStatePB& cstate) {
  if (cstate.leader_uuid() == permanent_uuid) {
    if (IsRaftConfigVoter(permanent_uuid, cstate.config())) {
      return RaftPeerPB::LEADER;
    }
    return RaftPeerPB::NON_PARTICIPANT;
  }

  for (const RaftPeerPB& peer : cstate.config().peers()) {
    if (peer.permanent_uuid() == permanent_uuid) {
      switch (peer.member_type()) {
        case RaftPeerPB::VOTER:
          return RaftPeerPB::FOLLOWER;

        // PRE_VOTER, PRE_OBSERVER peers are considered LEARNERs.
        case RaftPeerPB::PRE_VOTER:
        case RaftPeerPB::PRE_OBSERVER:
          return RaftPeerPB::LEARNER;

        case RaftPeerPB::OBSERVER:
          return RaftPeerPB::READ_REPLICA;

        case RaftPeerPB::UNKNOWN_MEMBER_TYPE:
         return RaftPeerPB::UNKNOWN_ROLE;
      }
    }
  }
  return RaftPeerPB::NON_PARTICIPANT;
}

Status VerifyRaftConfig(const RaftConfigPB& config, RaftConfigState type) {
  std::set<string> uuids;
  if (config.peers_size() == 0) {
    return STATUS(IllegalState,
        Substitute("RaftConfig must have at least one peer. RaftConfig: $0",
                   config.ShortDebugString()));
  }

  if (type == COMMITTED_QUORUM) {
    // Committed configurations must have 'opid_index' populated.
    if (!config.has_opid_index()) {
      return STATUS(IllegalState,
          Substitute("Committed configs must have opid_index set. RaftConfig: $0",
                     config.ShortDebugString()));
    }
  } else if (type == UNCOMMITTED_QUORUM) {
    // Uncommitted configurations must *not* have 'opid_index' populated.
    if (config.has_opid_index()) {
      return STATUS(IllegalState,
          Substitute("Uncommitted configs must not have opid_index set. RaftConfig: $0",
                     config.ShortDebugString()));
    }
  }

  int num_peers = config.peers_size();
  for (const RaftPeerPB& peer : config.peers()) {
    if (!peer.has_permanent_uuid() || peer.permanent_uuid() == "") {
      return STATUS(IllegalState, Substitute("One peer didn't have an uuid or had the empty"
          " string. RaftConfig: $0", config.ShortDebugString()));
    }
    if (ContainsKey(uuids, peer.permanent_uuid())) {
      return STATUS(IllegalState,
          Substitute("Found multiple peers with uuid: $0. RaftConfig: $1",
                     peer.permanent_uuid(), config.ShortDebugString()));
    }
    uuids.insert(peer.permanent_uuid());

    if (num_peers > 1 && !peer.has_last_known_addr()) {
      return STATUS(IllegalState,
          Substitute("Peer: $0 has no address. RaftConfig: $1",
                     peer.permanent_uuid(), config.ShortDebugString()));
    }
    if (!peer.has_member_type()) {
      return STATUS(IllegalState,
          Substitute("Peer: $0 has no member type set. RaftConfig: $1", peer.permanent_uuid(),
                     config.ShortDebugString()));
    }
  }

  return Status::OK();
}

Status VerifyConsensusState(const ConsensusStatePB& cstate, RaftConfigState type) {
  if (!cstate.has_current_term()) {
    return STATUS(IllegalState, "ConsensusStatePB missing current_term", cstate.ShortDebugString());
  }
  if (!cstate.has_config()) {
    return STATUS(IllegalState, "ConsensusStatePB missing config", cstate.ShortDebugString());
  }
  RETURN_NOT_OK(VerifyRaftConfig(cstate.config(), type));

  if (cstate.has_leader_uuid() && !cstate.leader_uuid().empty()) {
    if (!IsRaftConfigVoter(cstate.leader_uuid(), cstate.config())) {
      return STATUS(IllegalState,
          Substitute("Leader with UUID $0 is not a VOTER in the config! Consensus state: $1",
                     cstate.leader_uuid(), cstate.ShortDebugString()));
    }
  }

  return Status::OK();
}

} // namespace consensus
}  // namespace yb
