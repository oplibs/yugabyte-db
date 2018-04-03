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

#include "yb/tools/ysck_remote.h"

#include "yb/common/schema.h"
#include "yb/common/wire_protocol.h"
#include "yb/gutil/map-util.h"
#include "yb/gutil/strings/substitute.h"
#include "yb/util/net/net_util.h"
#include "yb/util/net/sockaddr.h"

DEFINE_bool(checksum_cache_blocks, false, "Should the checksum scanners cache the read blocks");
DEFINE_int64(timeout_ms, 1000 * 60, "RPC timeout in milliseconds");
DEFINE_int64(tablets_batch_size_max, 100, "How many tablets to get from the Master per RPC");
DECLARE_int64(outbound_rpc_block_size);
DECLARE_int64(outbound_rpc_memory_limit);

namespace yb {
namespace tools {

static const char kMessengerName[] = "ysck";

using rpc::Messenger;
using rpc::MessengerBuilder;
using rpc::RpcController;
using std::shared_ptr;
using std::string;
using std::vector;
using strings::Substitute;
using client::YBTableName;

MonoDelta GetDefaultTimeout() {
  return MonoDelta::FromMilliseconds(FLAGS_timeout_ms);
}

Status RemoteYsckTabletServer::Connect() const {
  server::PingRequestPB req;
  server::PingResponsePB resp;
  RpcController rpc;
  rpc.set_timeout(GetDefaultTimeout());
  return generic_proxy_->Ping(req, &resp, &rpc);
}

Status RemoteYsckTabletServer::CurrentHybridTime(uint64_t* hybrid_time) const {
  server::ServerClockRequestPB req;
  server::ServerClockResponsePB resp;
  RpcController rpc;
  rpc.set_timeout(GetDefaultTimeout());
  RETURN_NOT_OK(generic_proxy_->ServerClock(req, &resp, &rpc));
  CHECK(resp.has_hybrid_time());
  *hybrid_time = resp.hybrid_time();
  return Status::OK();
}

class ChecksumStepper;

// Simple class to act as a callback in order to collate results from parallel
// checksum scans.
class ChecksumCallbackHandler {
 public:
  explicit ChecksumCallbackHandler(ChecksumStepper* const stepper)
      : stepper(DCHECK_NOTNULL(stepper)) {
  }

  // Invoked by an RPC completion callback. Simply calls back into the stepper.
  // Then the call to the stepper returns, deletes 'this'.
  void Run();

 private:
  ChecksumStepper* const stepper;
};

// Simple class to have a "conversation" over multiple requests to a server
// to carry out a multi-part checksum scan.
// If any errors or timeouts are encountered, the checksum operation fails.
// After the ChecksumStepper reports its results to the reporter, it deletes itself.
class ChecksumStepper {
 public:
  ChecksumStepper(string tablet_id, const Schema& schema, string server_uuid,
                  ChecksumOptions options, ReportResultCallback callback,
                  shared_ptr<tserver::TabletServerServiceProxy> proxy)
      : schema_(schema),
        tablet_id_(std::move(tablet_id)),
        server_uuid_(std::move(server_uuid)),
        options_(std::move(options)),
        reporter_callback_(std::move(callback)),
        proxy_(std::move(proxy)) {
    DCHECK(proxy_);
  }

  void Start() {
    Status s = SchemaToColumnPBs(schema_, &cols_, SCHEMA_PB_WITHOUT_IDS);
    if (!s.ok()) {
      reporter_callback_.Run(s, 0);
    } else {
      SendRequest();
    }
  }

  void HandleResponse() {
    gscoped_ptr<ChecksumStepper> deleter(this);
    Status s = rpc_.status();
    if (s.ok() && resp_.has_error()) {
      s = StatusFromPB(resp_.error().status());
    }
    if (!s.ok()) {
      reporter_callback_.Run(s, 0);
      return; // Deletes 'this'.
    }

    DCHECK(resp_.has_checksum());

    reporter_callback_.Run(s, resp_.checksum());
  }

 private:
  void SendRequest() {
    req_.set_tablet_id(tablet_id_);
    req_.set_consistency_level(YBConsistencyLevel::CONSISTENT_PREFIX);
    rpc_.set_timeout(GetDefaultTimeout());
    auto handler = std::make_unique<ChecksumCallbackHandler>(this);
    rpc::ResponseCallback cb = std::bind(&ChecksumCallbackHandler::Run, handler.get());
    proxy_->ChecksumAsync(req_, &resp_, &rpc_, cb);
    ignore_result(handler.release());
  }

  const Schema schema_;
  google::protobuf::RepeatedPtrField<ColumnSchemaPB> cols_;

  const string tablet_id_;
  const string server_uuid_;
  const ChecksumOptions options_;
  const ReportResultCallback reporter_callback_;
  const shared_ptr<tserver::TabletServerServiceProxy> proxy_;

  tserver::ChecksumRequestPB req_;
  tserver::ChecksumResponsePB resp_;
  RpcController rpc_;
};

void ChecksumCallbackHandler::Run() {
  stepper->HandleResponse();
  delete this;
}

void RemoteYsckTabletServer::RunTabletChecksumScanAsync(
        const string& tablet_id,
        const Schema& schema,
        const ChecksumOptions& options,
        const ReportResultCallback& callback) {
  gscoped_ptr<ChecksumStepper> stepper(
      new ChecksumStepper(tablet_id, schema, uuid(), options, callback, ts_proxy_));
  stepper->Start();
  ignore_result(stepper.release()); // Deletes self on callback.
}

Status RemoteYsckMaster::Connect() const {
  server::PingRequestPB req;
  server::PingResponsePB resp;
  RpcController rpc;
  rpc.set_timeout(GetDefaultTimeout());
  return generic_proxy_->Ping(req, &resp, &rpc);
}

Status RemoteYsckMaster::Build(const Endpoint& address, shared_ptr<YsckMaster>* master) {
  MessengerBuilder builder(kMessengerName);
  builder.CreateConnectionContextFactory<rpc::YBConnectionContext>(
      FLAGS_outbound_rpc_block_size, FLAGS_outbound_rpc_memory_limit,
      MemTracker::CreateTracker("RemoteYsckMaster"));
  auto messenger = builder.Build();
  RETURN_NOT_OK(messenger);
  master->reset(new RemoteYsckMaster(address, *messenger));
  return Status::OK();
}

Status RemoteYsckMaster::RetrieveTabletServers(TSMap* tablet_servers) {
  master::ListTabletServersRequestPB req;
  master::ListTabletServersResponsePB resp;
  RpcController rpc;

  rpc.set_timeout(GetDefaultTimeout());
  RETURN_NOT_OK(proxy_->ListTabletServers(req, &resp, &rpc));
  tablet_servers->clear();
  for (const master::ListTabletServersResponsePB_Entry& e : resp.servers()) {
    HostPortPB addr = e.registration().common().rpc_addresses(0);
    std::vector<Endpoint> addresses;
    RETURN_NOT_OK(ParseAddressList(HostPort(addr.host(), addr.port()).ToString(),
                                   tserver::TabletServer::kDefaultPort, &addresses));
    shared_ptr<YsckTabletServer> ts(
        new RemoteYsckTabletServer(e.instance_id().permanent_uuid(), addresses[0], messenger_));
    InsertOrDie(tablet_servers, ts->uuid(), ts);
  }
  return Status::OK();
}

Status RemoteYsckMaster::RetrieveTablesList(vector<shared_ptr<YsckTable> >* tables) {
  master::ListTablesRequestPB req;
  master::ListTablesResponsePB resp;
  RpcController rpc;

  rpc.set_timeout(GetDefaultTimeout());
  RETURN_NOT_OK(proxy_->ListTables(req, &resp, &rpc));
  if (resp.has_error()) {
    return StatusFromPB(resp.error().status());
  }
  vector<shared_ptr<YsckTable> > tables_temp;
  for (const master::ListTablesResponsePB_TableInfo& info : resp.tables()) {
    Schema schema;
    int num_replicas = 0;
    DCHECK(info.has_namespace_());
    DCHECK(info.namespace_().has_name());
    YBTableName name(info.namespace_().name(), info.name());
    RETURN_NOT_OK(GetTableInfo(name, &schema, &num_replicas));
    LOG(INFO) << __func__ << ": name=" << name.ToString() << ", num_replicas=" << num_replicas;
    shared_ptr<YsckTable> table(new YsckTable(name, schema, num_replicas, info.table_type()));
    tables_temp.push_back(table);
  }
  tables->assign(tables_temp.begin(), tables_temp.end());
  return Status::OK();
}

Status RemoteYsckMaster::RetrieveTabletsList(const shared_ptr<YsckTable>& table) {
  vector<shared_ptr<YsckTablet> > tablets;
  bool more_tablets = true;
  string last_key;
  while (more_tablets) {
    RETURN_NOT_OK(GetTabletsBatch(table->name(), &last_key, &tablets, &more_tablets));
  }

  table->set_tablets(tablets);
  return Status::OK();
}

Status RemoteYsckMaster::GetTabletsBatch(const YBTableName& table_name,
                                         string* last_partition_key,
                                         vector<shared_ptr<YsckTablet> >* tablets,
                                         bool* more_tablets) {
  master::GetTableLocationsRequestPB req;
  master::GetTableLocationsResponsePB resp;
  RpcController rpc;

  table_name.SetIntoTableIdentifierPB(req.mutable_table());
  req.set_max_returned_locations(FLAGS_tablets_batch_size_max);
  req.set_partition_key_start(*last_partition_key);

  rpc.set_timeout(GetDefaultTimeout());
  RETURN_NOT_OK(proxy_->GetTableLocations(req, &resp, &rpc));
  for (const master::TabletLocationsPB& locations : resp.tablet_locations()) {
    shared_ptr<YsckTablet> tablet(new YsckTablet(locations.tablet_id()));
    vector<shared_ptr<YsckTabletReplica> > replicas;
    for (const master::TabletLocationsPB_ReplicaPB& replica : locations.replicas()) {
      bool is_leader = replica.role() == consensus::RaftPeerPB::LEADER;
      bool is_follower = replica.role() == consensus::RaftPeerPB::FOLLOWER;
      replicas.push_back(shared_ptr<YsckTabletReplica>(
          new YsckTabletReplica(replica.ts_info().permanent_uuid(), is_leader, is_follower)));
    }
    tablet->set_replicas(replicas);
    tablets->push_back(tablet);
  }
  if (resp.tablet_locations_size() != 0) {
    *last_partition_key = (resp.tablet_locations().end() - 1)->partition().partition_key_end();
  } else {
    return STATUS(NotFound, Substitute(
      "The Master returned 0 tablets for GetTableLocations of table $0 at start key $1",
      table_name.ToString(), *(last_partition_key)));
  }
  if (last_partition_key->empty()) {
    *more_tablets = false;
  }
  return Status::OK();
}

Status RemoteYsckMaster::GetTableInfo(const YBTableName& table_name,
                                      Schema* schema,
                                      int* num_replicas) {
  master::GetTableSchemaRequestPB req;
  master::GetTableSchemaResponsePB resp;
  RpcController rpc;

  table_name.SetIntoTableIdentifierPB(req.mutable_table());

  rpc.set_timeout(GetDefaultTimeout());
  RETURN_NOT_OK(proxy_->GetTableSchema(req, &resp, &rpc));

  RETURN_NOT_OK(SchemaFromPB(resp.schema(), schema));
  *num_replicas = resp.replication_info().live_replicas().num_replicas();
  return Status::OK();
}

} // namespace tools
} // namespace yb
