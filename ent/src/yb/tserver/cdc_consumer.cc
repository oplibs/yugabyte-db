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

#include <shared_mutex>
#include <chrono>

#include "yb/rpc/rpc.h"
#include "yb/tserver/cdc_consumer.h"
#include "yb/tserver/twodc_output_client.h"
#include "yb/tserver/tablet_server.h"
#include "yb/tserver/cdc_poller.h"

#include "yb/cdc/cdc_consumer.pb.h"

#include "yb/client/client.h"

#include "yb/gutil/map-util.h"
#include "yb/util/shared_lock.h"
#include "yb/util/string_util.h"
#include "yb/util/thread.h"

DECLARE_int32(cdc_read_rpc_timeout_ms);
DECLARE_int32(cdc_write_rpc_timeout_ms);

using namespace std::chrono_literals;

namespace yb {

namespace tserver {
namespace enterprise {

Result<std::unique_ptr<CDCConsumer>> CDCConsumer::Create(
    std::function<bool(const std::string&)> is_leader_for_tablet,
    rpc::ProxyCache* proxy_cache,
    TabletServer* tserver) {
  LOG(INFO) << "Creating CDC Consumer";
  auto master_addrs = tserver->options().GetMasterAddresses();
  std::vector<std::string> hostport_strs;
  hostport_strs.reserve(master_addrs->size());
  for (const auto& hp : *master_addrs) {
    hostport_strs.push_back(HostPort::ToCommaSeparatedString(hp));
  }

  auto local_client = VERIFY_RESULT(client::YBClientBuilder()
      .master_server_addrs(hostport_strs)
      .set_client_name("CDCConsumerLocal")
      .default_rpc_timeout(MonoDelta::FromMilliseconds(FLAGS_cdc_write_rpc_timeout_ms))
      .Build());

  auto cdc_consumer = std::make_unique<CDCConsumer>( std::move(is_leader_for_tablet), proxy_cache,
      tserver->permanent_uuid(), std::move(local_client));

  // TODO(NIC): Unify cdc_consumer thread_pool & remote_client_ threadpools
  RETURN_NOT_OK(yb::Thread::Create(
      "CDCConsumer", "Poll", &CDCConsumer::RunThread, cdc_consumer.get(),
      &cdc_consumer->run_trigger_poll_thread_));
  RETURN_NOT_OK(ThreadPoolBuilder("CDCConsumerHandler").Build(&cdc_consumer->thread_pool_));
  return cdc_consumer;
}

CDCConsumer::CDCConsumer(std::function<bool(const std::string&)> is_leader_for_tablet,
                         rpc::ProxyCache* proxy_cache,
                         const string& ts_uuid,
                         std::unique_ptr<client::YBClient> local_client) :
  is_leader_for_tablet_(std::move(is_leader_for_tablet)),
  log_prefix_(Format("[TS $0]: ", ts_uuid)),
  local_client_(std::move(local_client)) {}

CDCConsumer::~CDCConsumer() {
  Shutdown();
}

void CDCConsumer::Shutdown() {
  LOG_WITH_PREFIX(INFO) << "Shutting down CDC Consumer";
  {
    std::lock_guard<std::mutex> l(should_run_mutex_);
    should_run_ = false;
  }
  cond_.notify_all();

  {
    std::lock_guard<rw_spinlock> write_lock(master_data_mutex_);
    producer_consumer_tablet_map_from_master_.clear();
    uuid_master_addrs_.clear();
    {
      SharedLock<rw_spinlock> read_lock(producer_pollers_map_mutex_);
      for (auto &uuid_and_client : remote_clients_) {
        uuid_and_client.second->Shutdown();
      }
    }
    local_client_->Shutdown();
  }

  if (run_trigger_poll_thread_) {
    WARN_NOT_OK(ThreadJoiner(run_trigger_poll_thread_.get()).Join(), "Could not join thread");
  }

  if (thread_pool_) {
    thread_pool_->Shutdown();
  }
}

void CDCConsumer::RunThread() {
  while (true) {
    std::unique_lock<std::mutex> l(should_run_mutex_);
    if (!should_run_) {
      return;
    }
    cond_.wait_for(l, 1000ms);
    if (!should_run_) {
      return;
    }
    TriggerPollForNewTablets();
  }
}

void CDCConsumer::RefreshWithNewRegistryFromMaster(const cdc::ConsumerRegistryPB* consumer_registry,
                                                   int32_t cluster_config_version) {
  UpdateInMemoryState(consumer_registry, cluster_config_version);
  cond_.notify_all();
}

std::vector<std::string> CDCConsumer::TEST_producer_tablets_running() {
  SharedLock<rw_spinlock> read_lock(producer_pollers_map_mutex_);

  std::vector<string> tablets;
  for (const auto& producer : producer_pollers_map_) {
    tablets.push_back(producer.first.tablet_id);
  }
  return tablets;
}

// NOTE: This happens on TS.heartbeat, so it needs to finish quickly
void CDCConsumer::UpdateInMemoryState(const cdc::ConsumerRegistryPB* consumer_registry,
    int32_t cluster_config_version) {
  std::lock_guard<rw_spinlock> write_lock_master(master_data_mutex_);

  // Only update it if the version is newer.
  if (cluster_config_version <= cluster_config_version_.load(std::memory_order_acquire)) {
    return;
  }

  cluster_config_version_.store(cluster_config_version, std::memory_order_release);
  producer_consumer_tablet_map_from_master_.clear();
  uuid_master_addrs_.clear();

  if (!consumer_registry) {
    LOG_WITH_PREFIX(INFO) << "Given empty CDC consumer registry: removing Pollers";
    cond_.notify_all();
    return;
  }

  LOG_WITH_PREFIX(INFO) << "Updating CDC consumer registry: " << consumer_registry->DebugString();

  for (const auto& producer_map : DCHECK_NOTNULL(consumer_registry)->producer_map()) {
    const auto& producer_entry_pb = producer_map.second;
    if (producer_entry_pb.disable_stream()) {
      continue;
    }
    // recreate the UUID connection information
    if (!ContainsKey(uuid_master_addrs_, producer_map.first)) {
      std::vector<HostPort> hp;
      HostPortsFromPBs(producer_map.second.master_addrs(), &hp);
      uuid_master_addrs_[producer_map.first] = HostPort::ToCommaSeparatedString(hp);
    }
    // recreate the set of CDCPollers
    for (const auto& stream_entry : producer_entry_pb.stream_map()) {
      const auto& stream_entry_pb = stream_entry.second;
      for (const auto& tablet_entry : stream_entry_pb.consumer_producer_tablet_map()) {
        const auto& consumer_tablet_id = tablet_entry.first;
        for (const auto& producer_tablet_id : tablet_entry.second.tablets()) {
          cdc::ProducerTabletInfo producer_tablet_info(
              {producer_map.first, stream_entry.first, producer_tablet_id});
          cdc::ConsumerTabletInfo consumer_tablet_info(
              {consumer_tablet_id, stream_entry_pb.consumer_table_id()});
          producer_consumer_tablet_map_from_master_[producer_tablet_info] = consumer_tablet_info;
        }
      }
    }
  }
  cond_.notify_all();
}

void CDCConsumer::TriggerPollForNewTablets() {
  SharedLock<rw_spinlock> read_lock_master(master_data_mutex_);

  for (const auto& entry : producer_consumer_tablet_map_from_master_) {
    bool start_polling;
    {
      SharedLock<rw_spinlock> read_lock_pollers(producer_pollers_map_mutex_);
      start_polling = producer_pollers_map_.find(entry.first) == producer_pollers_map_.end() &&
                      is_leader_for_tablet_(entry.second.tablet_id);
    }
    if (start_polling) {
      std::lock_guard <rw_spinlock> write_lock_pollers(producer_pollers_map_mutex_);

      // Check again, since we unlocked.
      start_polling = producer_pollers_map_.find(entry.first) == producer_pollers_map_.end() &&
          is_leader_for_tablet_(entry.second.tablet_id);
      if (start_polling) {
        // This is a new tablet, trigger a poll.
        auto uuid = entry.first.universe_uuid;

        // See if we need to create a new client connection
        if (!ContainsKey(remote_clients_, uuid)) {
          CHECK(ContainsKey(uuid_master_addrs_, uuid));
          auto client_result = yb::client::YBClientBuilder()
              .set_client_name("CDCConsumerRemote::" + uuid)
              .add_master_server_addr(uuid_master_addrs_[uuid])
              .skip_master_flagfile()
              .default_rpc_timeout(MonoDelta::FromMilliseconds(FLAGS_cdc_read_rpc_timeout_ms))
              .Build();
          if (!client_result.ok()) {
            LOG(WARNING) << "Could not create a new YBClient for " << uuid
                         << ": " << client_result.status().ToString();
            return; // Don't finish creation.  Try again on the next heartbeat.
          }
          remote_clients_[uuid] = CHECK_RESULT(client_result);
        }

        // now create the poller
        auto cdc_poller = std::make_shared<CDCPoller>(
            entry.first, entry.second,
            std::bind(&CDCConsumer::ShouldContinuePolling, this, entry.first),
            std::bind(&CDCConsumer::RemoveFromPollersMap, this, entry.first),
            thread_pool_.get(),
            local_client_,
            remote_clients_[uuid],
            this);
        LOG_WITH_PREFIX(INFO) << Format("Start polling for producer tablet $0",
            entry.first.tablet_id);
        producer_pollers_map_[entry.first] = cdc_poller;
        cdc_poller->Poll();
      }
    }
  }
}

void CDCConsumer::RemoveFromPollersMap(const cdc::ProducerTabletInfo producer_tablet_info) {
  LOG_WITH_PREFIX(INFO) << Format("Stop polling for producer tablet $0",
                                  producer_tablet_info.tablet_id);
  std::shared_ptr<client::YBClient> client_to_delete; // decrement refcount to 0 outside lock
  {
    SharedLock<rw_spinlock> read_lock_master(master_data_mutex_);
    std::lock_guard<rw_spinlock> write_lock_pollers(producer_pollers_map_mutex_);
    producer_pollers_map_.erase(producer_tablet_info);
    // Check if no more objects with this UUID exist after registry refresh.
    if (!ContainsKey(uuid_master_addrs_, producer_tablet_info.universe_uuid)) {
      auto it = remote_clients_.find(producer_tablet_info.universe_uuid);
      if (it != remote_clients_.end()) {
        client_to_delete = it->second;
        remote_clients_.erase(it);
      }
    }
  }
  if (client_to_delete != nullptr) {
    client_to_delete->Shutdown();
  }
}

bool CDCConsumer::ShouldContinuePolling(const cdc::ProducerTabletInfo producer_tablet_info) {
  std::lock_guard<std::mutex> l(should_run_mutex_);
  if (!should_run_) {
    return false;
  }

  SharedLock<rw_spinlock> read_lock_master(master_data_mutex_);

  const auto& it = producer_consumer_tablet_map_from_master_.find(producer_tablet_info);
  if (it == producer_consumer_tablet_map_from_master_.end()) {
    // We no longer care about this tablet, abort the cycle.
    return false;
  }
  return is_leader_for_tablet_(it->second.tablet_id);
}

std::string CDCConsumer::LogPrefix() {
  return log_prefix_;
}

int32_t CDCConsumer::cluster_config_version() const {
  return cluster_config_version_.load(std::memory_order_acquire);
}

} // namespace enterprise
} // namespace tserver
} // namespace yb
