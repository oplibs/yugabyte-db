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

#ifndef YB_CLIENT_ASYNC_RPC_H_
#define YB_CLIENT_ASYNC_RPC_H_

#include "yb/rpc/rpc_fwd.h"

#include "yb/tserver/tserver_service.proxy.h"

#include "yb/client/tablet_rpc.h"

namespace yb {
namespace client {

class YBTable;
class YBClient;

namespace internal {

class Batcher;
struct InFlightOp;
class RemoteTablet;
class RemoteTabletServer;

// Container for async rpc metrics
struct AsyncRpcMetrics {
  explicit AsyncRpcMetrics(const scoped_refptr<MetricEntity>& metric_entity);

  scoped_refptr<Histogram> remote_write_rpc_time;
  scoped_refptr<Histogram> remote_read_rpc_time;
  scoped_refptr<Histogram> local_write_rpc_time;
  scoped_refptr<Histogram> local_read_rpc_time;
  scoped_refptr<Histogram> time_to_send;
};

typedef std::shared_ptr<AsyncRpcMetrics> AsyncRpcMetricsPtr;

// An Async RPC which is in-flight to a tablet. Initially, the RPC is sent
// to the leader replica, but it may be retried with another replica if the
// leader fails.
//
// Keeps a reference on the owning batcher while alive. It doesn't take a generic callback,
// but ProcessResponseFromTserver will update the state after getting the end response.
// This class deletes itself after Rpc returns and is processed.
class AsyncRpc : public rpc::Rpc, public TabletRpc {
 public:
  AsyncRpc(
      const scoped_refptr<Batcher>& batcher, RemoteTablet* const tablet,
      bool allow_local_calls_in_curr_thread, InFlightOps ops,
      YBConsistencyLevel yb_consistency_level);

  virtual ~AsyncRpc();

  void SendRpc() override;
  string ToString() const override;

  const YBTable* table() const;
  const RemoteTablet& tablet() const { return *tablet_invoker_.tablet(); }
  const InFlightOps& ops() const { return ops_; }

 protected:
  void Finished(const Status& status) override;

  void SendRpcToTserver() override;

  virtual void CallRemoteMethod() = 0;

  // This is the last step where errors and responses are collected from the response and
  // stored in batcher. If there's a callback from the user, it is done in this step.
  virtual void ProcessResponseFromTserver(const Status& status) = 0;

  // Return latest hybrid time that was present on tserver during processing of this request.
  virtual HybridTime PropagatedHybridTime() = 0;

  void Failed(const Status& status) override;

  // Is this a local call?
  bool IsLocalCall() const;

  // Pointer back to the batcher. Processes the write response when it
  // completes, regardless of success or failure.
  scoped_refptr<Batcher> batcher_;

  // The trace buffer.
  scoped_refptr<Trace> trace_;

  TabletInvoker tablet_invoker_;

  // Operations which were batched into this RPC.
  // These operations are in kRequestSent state.
  InFlightOps ops_;

  MonoTime start_;
  std::shared_ptr<AsyncRpcMetrics> async_rpc_metrics_;
  rpc::RpcCommandPtr retained_self_;
};

template <class Req, class Resp>
class AsyncRpcBase : public AsyncRpc {
 public:
  AsyncRpcBase(
      const scoped_refptr<Batcher>& batcher, RemoteTablet* const tablet,
      bool allow_local_calls_in_curr_thread, InFlightOps ops, YBConsistencyLevel consistency_level);

  const Resp& resp() const { return resp_; }
  Resp& resp() { return resp_; }

 protected:
  // Returns `true` if caller should continue processing response, `false` otherwise.
  bool CommonResponseCheck(const Status& status);

 protected: // TODO replace with private
  const tserver::TabletServerErrorPB* response_error() const override {
    return resp_.has_error() ? &resp_.error() : nullptr;
  }

  HybridTime PropagatedHybridTime() override {
    return GetPropagatedHybridTime(resp_);
  }

  Req req_;
  Resp resp_;
};

class WriteRpc : public AsyncRpcBase<tserver::WriteRequestPB, tserver::WriteResponsePB> {
 public:
  WriteRpc(
      const scoped_refptr<Batcher>& batcher, RemoteTablet* const tablet,
      bool allow_local_calls_in_curr_thread, InFlightOps ops);

  virtual ~WriteRpc();

 private:
  void CallRemoteMethod() override;
  void ProcessResponseFromTserver(const Status& status) override;
};

class ReadRpc : public AsyncRpcBase<tserver::ReadRequestPB, tserver::ReadResponsePB> {
 public:
  ReadRpc(
      const scoped_refptr<Batcher>& batcher, RemoteTablet* const tablet,
      bool allow_local_calls_in_curr_thread, InFlightOps ops,
      YBConsistencyLevel yb_consistency_level = YBConsistencyLevel::STRONG);

  virtual ~ReadRpc();

 private:
  void CallRemoteMethod() override;
  void ProcessResponseFromTserver(const Status& status) override;
};

}  // namespace internal
}  // namespace client
}  // namespace yb

#endif  // YB_CLIENT_ASYNC_RPC_H_
