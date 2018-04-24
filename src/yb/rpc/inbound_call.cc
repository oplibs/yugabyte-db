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

#include "yb/rpc/inbound_call.h"

#include "yb/common/redis_protocol.pb.h"

#include "yb/gutil/strings/substitute.h"

#include "yb/rpc/connection.h"
#include "yb/rpc/connection_context.h"
#include "yb/rpc/rpc_introspection.pb.h"
#include "yb/rpc/serialization.h"
#include "yb/rpc/service_pool.h"

#include "yb/util/debug/trace_event.h"
#include "yb/util/flag_tags.h"
#include "yb/util/logging.h"
#include "yb/util/metrics.h"
#include "yb/util/trace.h"
#include "yb/util/memory/memory.h"

using std::shared_ptr;
using std::vector;
using strings::Substitute;
using yb::RedisResponsePB;

DEFINE_bool(rpc_dump_all_traces, false,
            "If true, dump all RPC traces at INFO level");
TAG_FLAG(rpc_dump_all_traces, advanced);
TAG_FLAG(rpc_dump_all_traces, runtime);

DEFINE_bool(collect_end_to_end_traces, false,
            "If true, collected traces includes information for sub-components "
            "potentially running on a different server. ");
TAG_FLAG(collect_end_to_end_traces, advanced);
TAG_FLAG(collect_end_to_end_traces, runtime);

DEFINE_int32(print_trace_every, 0,
             "Controls the rate at which traces are printed. Setting this to 0 "
             "disables printing the collected traces.");
TAG_FLAG(print_trace_every, advanced);
TAG_FLAG(print_trace_every, runtime);

DEFINE_int32(rpc_slow_query_threshold_ms, 10000,
             "Traces for calls that take longer than this threshold (in ms) are logged");
TAG_FLAG(rpc_slow_query_threshold_ms, advanced);
TAG_FLAG(rpc_slow_query_threshold_ms, runtime);

namespace yb {
namespace rpc {

InboundCall::InboundCall(ConnectionPtr conn, CallProcessedListener call_processed_listener)
    : trace_(new Trace),
      conn_(std::move(conn)),
      call_processed_listener_(std::move(call_processed_listener)) {
  TRACE_TO(trace_, "Created InboundCall");
  RecordCallReceived();
}

InboundCall::~InboundCall() {
  TRACE_TO(trace_, "Destroying InboundCall");
  YB_LOG_IF_EVERY_N(INFO, FLAGS_print_trace_every > 0, FLAGS_print_trace_every)
      << "Tracing op: \n " << trace_->DumpToString(true);
}

void InboundCall::NotifyTransferred(const Status& status, Connection* conn) {
  if (status.ok()) {
    TRACE_TO(trace_, "Transfer finished");
  } else {
    YB_LOG_EVERY_N_SECS(WARNING, 10) << LogPrefix() << "Connection torn down before " << ToString()
                                     << " could send its response: " << status.ToString();
  }
  if (call_processed_listener_) {
    call_processed_listener_(this);
  }
}

const Endpoint& InboundCall::remote_address() const {
  CHECK_NOTNULL(conn_.get());
  return conn_->remote();
}

const Endpoint& InboundCall::local_address() const {
  CHECK_NOTNULL(conn_.get());
  return conn_->local();
}

ConnectionPtr InboundCall::connection() const {
  return conn_;
}

ConnectionContext& InboundCall::connection_context() const {
  return conn_->context();
}

Trace* InboundCall::trace() {
  return trace_.get();
}

void InboundCall::RecordCallReceived() {
  TRACE_EVENT_ASYNC_BEGIN0("rpc", "InboundCall", this);
  DCHECK(!timing_.time_received.Initialized());  // Protect against multiple calls.
  VLOG(4) << "Received call " << ToString();
  timing_.time_received = MonoTime::Now();
}

void InboundCall::RecordHandlingStarted(scoped_refptr<Histogram> incoming_queue_time) {
  DCHECK(incoming_queue_time != nullptr);
  DCHECK(!timing_.time_handled.Initialized());  // Protect against multiple calls.
  timing_.time_handled = MonoTime::Now();
  VLOG(4) << "Handling call " << ToString();
  incoming_queue_time->Increment(
      timing_.time_handled.GetDeltaSince(timing_.time_received).ToMicroseconds());
}

MonoDelta InboundCall::GetTimeInQueue() const {
  return timing_.time_handled.GetDeltaSince(timing_.time_received);
}

void InboundCall::RecordHandlingCompleted(scoped_refptr<Histogram> handler_run_time) {
  DCHECK(!timing_.time_completed.Initialized());  // Protect against multiple calls.
  timing_.time_completed = MonoTime::Now();
  VLOG(4) << "Completed handling call " << ToString();
  if (handler_run_time) {
    handler_run_time->Increment((timing_.time_completed - timing_.time_handled).ToMicroseconds());
  }
}

bool InboundCall::ClientTimedOut() const {
  auto deadline = GetClientDeadline();
  if (deadline.Equals(MonoTime::Max())) {
    return false;
  }

  MonoTime now = MonoTime::Now();
  return deadline.ComesBefore(now);
}

void InboundCall::QueueResponse(bool is_success) {
  TRACE_TO(trace_, is_success ? "Queueing success response" : "Queueing failure response");
  LogTrace();
  connection()->context().QueueResponse(connection(), shared_from(this));
}

std::string InboundCall::LogPrefix() const {
  return Format("{ InboundCall@$0 } ", this);
}

}  // namespace rpc
}  // namespace yb
