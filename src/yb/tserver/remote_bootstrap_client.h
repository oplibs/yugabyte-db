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
#ifndef YB_TSERVER_REMOTE_BOOTSTRAP_CLIENT_H
#define YB_TSERVER_REMOTE_BOOTSTRAP_CLIENT_H

#include <atomic>
#include <string>
#include <memory>
#include <vector>
#include <unordered_map>

#include <gtest/gtest_prod.h>

#include "yb/consensus/consensus_fwd.h"
#include "yb/gutil/gscoped_ptr.h"
#include "yb/gutil/macros.h"
#include "yb/gutil/ref_counted.h"
#include "yb/rpc/rpc_fwd.h"
#include "yb/tserver/remote_bootstrap.pb.h"
#include "yb/util/status.h"

namespace yb {

class BlockIdPB;
class FsManager;
class HostPort;

namespace consensus {
class ConsensusMetadata;
class ConsensusStatePB;
class RaftConfigPB;
class RaftPeerPB;
} // namespace consensus

namespace tablet {
class RaftGroupMetadata;
class TabletPeer;
class TabletStatusListener;
class RaftGroupReplicaSuperBlockPB;
} // namespace tablet

namespace tserver {
class DataIdPB;
class DataChunkPB;
class RemoteBootstrapServiceProxy;
class TSTabletManager;

// Client class for using remote bootstrap to copy a tablet from another host.
// This class is not thread-safe.
//
// TODO:
// * Parallelize download of blocks and WAL segments.
//
class RemoteBootstrapClient {
 public:

  // Construct the remote bootstrap client.
  // 'fs_manager' and 'messenger' must remain valid until this object is destroyed.
  // 'client_permanent_uuid' is the permanent UUID of the caller server.
  RemoteBootstrapClient(std::string tablet_id, FsManager* fs_manager,
                        std::string client_permanent_uuid);

  // Attempt to clean up resources on the remote end by sending an
  // EndRemoteBootstrapSession() RPC
  virtual ~RemoteBootstrapClient();

  // Pass in the existing metadata for a tombstoned tablet, which will be
  // replaced if validation checks pass in Start().
  // 'meta' is the metadata for the tombstoned tablet and 'caller_term' is the
  // term provided by the caller (assumed to be the current leader of the
  // consensus config) for validation purposes.
  // If the consensus metadata exists on disk for this tablet, and if
  // 'caller_term' is lower than the current term stored in that consensus
  // metadata, then this method will fail with a Status::InvalidArgument error.
  CHECKED_STATUS SetTabletToReplace(const scoped_refptr<tablet::RaftGroupMetadata>& meta,
                                    int64_t caller_term);

  // Start up a remote bootstrap session to bootstrap from the specified
  // bootstrap peer. Place a new superblock indicating that remote bootstrap is
  // in progress. If the 'metadata' pointer is passed as NULL, it is ignored,
  // otherwise the RaftGroupMetadata object resulting from the initial remote
  // bootstrap response is returned.
  // ts_manager pointer allows the bootstrap function to assign non-random
  // data and wal directories for the bootstrapped tablets.
  // TODO: Rename these parameters to bootstrap_source_*.
  CHECKED_STATUS Start(const std::string& bootstrap_peer_uuid,
                       rpc::ProxyCache* proxy_cache,
                       const HostPort& bootstrap_peer_addr,
                       scoped_refptr<tablet::RaftGroupMetadata>* metadata,
                       TSTabletManager* ts_manager = nullptr);

  // Runs a "full" remote bootstrap, copying the physical layout of a tablet
  // from the leader of the specified consensus configuration.
  virtual CHECKED_STATUS FetchAll(tablet::TabletStatusListener* status_listener);

  // After downloading all files successfully, write out the completed
  // replacement superblock.
  virtual CHECKED_STATUS Finish();

  // Verify that the remote bootstrap was completed successfully by verifying that the ChangeConfig
  // request was propagated.
  CHECKED_STATUS VerifyChangeRoleSucceeded(
      const std::shared_ptr<consensus::Consensus>& shared_consensus);

 protected:
  FRIEND_TEST(RemoteBootstrapRocksDBClientTest, TestBeginEndSession);
  FRIEND_TEST(RemoteBootstrapRocksDBClientTest, TestDownloadRocksDBFiles);

  // Extract the embedded Status message from the given ErrorStatusPB.
  // The given ErrorStatusPB must extend RemoteBootstrapErrorPB.
  static CHECKED_STATUS ExtractRemoteError(const rpc::ErrorStatusPB& remote_error);

  static CHECKED_STATUS UnwindRemoteError(const Status& status,
                                          const rpc::RpcController& controller);

  // Update the bootstrap StatusListener with a message.
  // The string "RemoteBootstrap: " will be prepended to each message.
  void UpdateStatusMessage(const std::string& message);

  // End the remote bootstrap session.
  CHECKED_STATUS EndRemoteSession();

  // Download all WAL files sequentially.
  CHECKED_STATUS DownloadWALs();

  // Download a single WAL file.
  // Assumes the WAL directories have already been created.
  // WAL file is opened with options so that it will fsync() on close.
  CHECKED_STATUS DownloadWAL(uint64_t wal_segment_seqno);

  // Write out the Consensus Metadata file based on the ConsensusStatePB
  // downloaded as part of initiating the remote bootstrap session.
  CHECKED_STATUS WriteConsensusMetadata();

  // Download a single remote file. The block and WAL implementations delegate
  // to this method when downloading files.
  //
  // An Appendable is typically a WritableFile (WAL).
  //
  // Only used in one compilation unit, otherwise the implementation would
  // need to be in the header.
  template<class Appendable>
  CHECKED_STATUS DownloadFile(const DataIdPB& data_id, Appendable* appendable);

  virtual CHECKED_STATUS CreateTabletDirectories(const string& db_dir, FsManager* fs);

  CHECKED_STATUS DownloadRocksDBFiles();

  CHECKED_STATUS VerifyData(uint64_t offset, const DataChunkPB& resp);

  CHECKED_STATUS DownloadFile(
      const tablet::FilePB& file_pb, const std::string& dir, DataIdPB* data_id);

  // Return standard log prefix.
  std::string LogPrefix();

  // Set-once members.
  const std::string tablet_id_;
  FsManager* const fs_manager_;
  const std::string permanent_uuid_;

  // State flags that enforce the progress of remote bootstrap.
  bool started_;            // Session started.
  // Total number of remote bootstrap sessions. Used to calculate the transmission rate across all
  // the sessions.
  static std::atomic<int32_t> n_started_;
  bool downloaded_wal_;     // WAL segments downloaded.
  bool downloaded_blocks_;  // Data blocks downloaded.
  bool downloaded_rocksdb_files_;

  // Session-specific data items.
  bool replace_tombstoned_tablet_;

  // Local tablet metadata file.
  scoped_refptr<tablet::RaftGroupMetadata> meta_;

  // Local Consensus metadata file. This may initially be NULL if this is
  // bootstrapping a new replica (rather than replacing an old one).
  std::unique_ptr<consensus::ConsensusMetadata> cmeta_;

  tablet::TabletStatusListener* status_listener_;
  std::shared_ptr<RemoteBootstrapServiceProxy> proxy_;
  std::string session_id_;
  uint64_t session_idle_timeout_millis_;
  gscoped_ptr<tablet::RaftGroupReplicaSuperBlockPB> superblock_;
  gscoped_ptr<tablet::RaftGroupReplicaSuperBlockPB> new_superblock_;
  gscoped_ptr<consensus::ConsensusStatePB> remote_committed_cstate_;
  std::vector<uint64_t> wal_seqnos_;

  int64_t start_time_micros_;

  // We track whether this session succeeded and send this information as part of the
  // EndRemoteBootstrapSessionRequestPB request.
  bool succeeded_;

 private:
  std::unordered_map<uint64_t, std::string> inode2file_;

  DISALLOW_COPY_AND_ASSIGN(RemoteBootstrapClient);
};

} // namespace tserver
} // namespace yb
#endif // YB_TSERVER_REMOTE_BOOTSTRAP_CLIENT_H
