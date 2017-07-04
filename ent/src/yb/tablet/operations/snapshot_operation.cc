//
// Copyright (c) YugaByte, Inc.
//

#include <glog/logging.h>

#include "yb/tablet/operations/snapshot_operation.h"

#include "yb/common/wire_protocol.h"
#include "yb/rpc/rpc_context.h"
#include "yb/server/hybrid_clock.h"
#include "yb/tablet/tablet.h"
#include "yb/tablet/tablet_peer.h"
#include "yb/tablet/tablet_metrics.h"
#include "yb/tserver/backup.pb.h"
#include "yb/util/trace.h"

namespace yb {
namespace tablet {

using std::bind;
using consensus::ReplicateMsg;
using consensus::CommitMsg;
using consensus::SNAPSHOT_OP;
using consensus::DriverType;
using strings::Substitute;
using yb::tserver::TabletServerErrorPB;
using yb::tserver::CreateTabletSnapshotRequestPB;
using yb::tserver::CreateTabletSnapshotResponsePB;

string SnapshotOperationState::ToString() const {
  return Substitute("SnapshotOperationState "
                    "[hybrid_time=$0, request=$1]",
                    hybrid_time().ToString(),
                    request_ == nullptr ? "(none)" : request_->ShortDebugString());
}

void SnapshotOperationState::AcquireSchemaLock(rw_semaphore* l) {
  TRACE("Acquiring schema lock in exclusive mode");
  schema_lock_ = std::unique_lock<rw_semaphore>(*l);
  TRACE("Acquired schema lock");
}

void SnapshotOperationState::ReleaseSchemaLock() {
  CHECK(schema_lock_.owns_lock());
  schema_lock_ = std::unique_lock<rw_semaphore>();
  TRACE("Released schema lock");
}

SnapshotOperation::SnapshotOperation(std::unique_ptr<SnapshotOperationState> state,
                                     DriverType type)
    : Operation(std::move(state), type, Operation::SNAPSHOT_TXN) {}

consensus::ReplicateMsgPtr SnapshotOperation::NewReplicateMsg() {
  auto result = std::make_shared<ReplicateMsg>();
  result->set_op_type(SNAPSHOT_OP);
  result->mutable_snapshot_request()->CopyFrom(*state()->request());
  return result;
}

Status SnapshotOperation::Prepare() {
  TRACE("PREPARE SNAPSHOT: Starting");
  TabletClass* tablet = state()->tablet_peer()->tablet();
  RETURN_NOT_OK(tablet->PrepareForCreateSnapshot(state()));

  TRACE("PREPARE SNAPSHOT: finished");
  return Status::OK();
}

void SnapshotOperation::Start() {
  DCHECK(state()->tablet_peer()->tablet()->table_type() == TableType::YQL_TABLE_TYPE);

  if (!state()->has_hybrid_time()) {
    state()->set_hybrid_time(state()->tablet_peer()->clock().Now());
  }

  TRACE("START. HybridTime: $0",
      server::HybridClock::GetPhysicalValueMicros(state()->hybrid_time()));
}

Status SnapshotOperation::Apply(gscoped_ptr<CommitMsg>* commit_msg) {
  TRACE("APPLY SNAPSHOT: Starting");

  TabletClass* const tablet = state()->tablet_peer()->tablet();
  RETURN_NOT_OK(tablet->CreateSnapshot(state()));

  commit_msg->reset(new CommitMsg());
  (*commit_msg)->set_op_type(SNAPSHOT_OP);
  return Status::OK();
}

void SnapshotOperation::Finish(OperationResult result) {
  if (PREDICT_FALSE(result == Operation::ABORTED)) {
    TRACE("SnapshotOperation: operation aborted");
    state()->Finish();
    return;
  }

  // The schema lock was acquired by Tablet::PrepareForCreateSnapshot.
  // Normally, we would release it in tablet.cc after applying the operation,
  // but currently we need to wait until after the COMMIT message is logged
  // to release this lock as a workaround for KUDU-915. See the same TODO in
  // AlterSchemaOperation().
  state()->ReleaseSchemaLock();

  DCHECK_EQ(result, Operation::COMMITTED);
  // Now that all of the changes have been applied and the commit is durable
  // make the changes visible to readers.
  TRACE("SnapshotOperation: making snapshot visible");
  state()->Finish();
}

string SnapshotOperation::ToString() const {
  return Substitute("SnapshotOperation [state=$0]", state()->ToString());
}

}  // namespace tablet
}  // namespace yb
