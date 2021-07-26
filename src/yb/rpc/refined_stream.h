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

#ifndef YB_RPC_REFINED_STREAM_H
#define YB_RPC_REFINED_STREAM_H

#include "yb/rpc/circular_read_buffer.h"
#include "yb/rpc/stream.h"

#include "yb/util/mem_tracker.h"

namespace yb {
namespace rpc {

YB_DEFINE_ENUM(RefinedStreamState, (kInitial)(kHandshake)(kEnabled)(kDisabled));
YB_DEFINE_ENUM(LocalSide, (kClient)(kServer));

// StreamRefiner is used by RefinedStream to perform actual stream data modification.
class StreamRefiner {
 public:
  virtual void Start(RefinedStream* stream) = 0;
  virtual Result<size_t> ProcessHeader(const IoVecs& data) = 0;
  virtual CHECKED_STATUS Send(OutboundDataPtr data) = 0;
  virtual CHECKED_STATUS Handshake() = 0;
  virtual Result<size_t> Read(void* buf, size_t num) = 0;
  virtual Result<size_t> Receive(const Slice& slice) = 0;
  virtual const Protocol* GetProtocol() = 0;

  virtual std::string ToString() const = 0;

  virtual ~StreamRefiner() = default;
};

// Stream that alters the data sent and received by lower layer streams.
// For instance it could be used to compress or encrypt the data.
//
// RefinedStream keeps the code common to all such streams,
// while StreamRefiner provides actual data modification.
class RefinedStream : public Stream, public StreamContext {
 public:
  RefinedStream(std::unique_ptr<Stream> lower_stream, std::unique_ptr<StreamRefiner> refiner,
                size_t receive_buffer_size, const MemTrackerPtr& buffer_tracker);

  size_t GetPendingWriteBytes() override;
  void Close() override;
  CHECKED_STATUS TryWrite() override;
  void ParseReceived() override;
  bool Idle(std::string* reason) override;
  void DumpPB(const DumpRunningRpcsRequestPB& req, RpcConnectionPB* resp) override;
  const Endpoint& Remote() const override;
  const Endpoint& Local() const override;
  CHECKED_STATUS Start(bool connect, ev::loop_ref* loop, StreamContext* context) override;
  void Shutdown(const Status& status) override;
  Result<size_t> Send(OutboundDataPtr data) override;
  void Cancelled(size_t handle) override;
  bool IsConnected() override;
  const Protocol* GetProtocol() override;
  StreamReadBuffer& ReadBuffer() override;
  std::string ToString() const override;

  // Implementation StreamContext
  Result<ProcessDataResult> ProcessReceived(
      const IoVecs& data, ReadBufferFull read_buffer_full) override;
  void Connected() override;

  void UpdateLastActivity() override;
  void UpdateLastRead() override;
  void UpdateLastWrite() override;
  void Transferred(const OutboundDataPtr& data, const Status& status) override;
  void Destroy(const Status& status) override;

  CHECKED_STATUS Established(RefinedStreamState state);
  CHECKED_STATUS SendToLower(OutboundDataPtr data);
  CHECKED_STATUS StartHandshake();

  StreamContext& context() const {
    return *context_;
  }

  LocalSide local_side() const {
    return local_side_;
  }

 private:
  CHECKED_STATUS HandshakeOrRead();
  CHECKED_STATUS Read();

  std::unique_ptr<Stream> lower_stream_;
  std::unique_ptr<StreamRefiner> refiner_;
  RefinedStreamState state_ = RefinedStreamState::kInitial;
  StreamContext* context_ = nullptr;
  std::vector<OutboundDataPtr> pending_data_;
  size_t lower_stream_bytes_to_skip_ = 0;
  LocalSide local_side_ = LocalSide::kServer;
  CircularReadBuffer read_buffer_;
};

class RefinedStreamFactory : public StreamFactory {
 public:
  using RefinerFactory = std::function<std::unique_ptr<StreamRefiner>(
        size_t receive_buffer_size, const MemTrackerPtr& buffer_tracker,
        const StreamCreateData& data)>;

  RefinedStreamFactory(
      StreamFactoryPtr lower_layer_factory, const MemTrackerPtr& buffer_tracker,
      RefinerFactory refiner_factory);

 private:
  std::unique_ptr<Stream> Create(const StreamCreateData& data) override;

  StreamFactoryPtr lower_layer_factory_;
  MemTrackerPtr buffer_tracker_;
  RefinerFactory refiner_factory_;
};

}  // namespace rpc
}  // namespace yb

#endif  // YB_RPC_REFINED_STREAM_H
