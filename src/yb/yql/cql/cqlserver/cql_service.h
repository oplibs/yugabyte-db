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
// This file contains the CQLServiceImpl class that implements the CQL server to handle requests
// from Cassandra clients using the CQL native protocol.

#ifndef YB_YQL_CQL_CQLSERVER_CQL_SERVICE_H_
#define YB_YQL_CQL_CQLSERVER_CQL_SERVICE_H_

#include <vector>

#include "yb/client/client_fwd.h"

#include "yb/yql/cql/cqlserver/cql_message.h"
#include "yb/yql/cql/cqlserver/cql_processor.h"
#include "yb/yql/cql/cqlserver/cql_statement.h"
#include "yb/yql/cql/cqlserver/cql_service.service.h"
#include "yb/yql/cql/cqlserver/cql_server_options.h"
#include "yb/yql/cql/ql/statement.h"

#include "yb/util/string_case.h"

#include "yb/client/async_initializer.h"

namespace yb {

namespace cqlserver {

extern const char* const kRoleColumnNameSaltedHash;
extern const char* const kRoleColumnNameCanLogin;

class CQLMetrics;
class CQLProcessor;
class CQLServer;

class CQLServiceImpl : public CQLServerServiceIf,
                       public GarbageCollector,
                       public std::enable_shared_from_this<CQLServiceImpl> {
 public:
  // Constructor.
  CQLServiceImpl(CQLServer* server, const CQLServerOptions& opts,
                 client::LocalTabletFilter local_tablet_filter);
  ~CQLServiceImpl();

  void CompleteInit();

  void Shutdown() override;

  // Processing all incoming request from RPC and sending response back.
  void Handle(yb::rpc::InboundCallPtr call) override;

  // Return CQL processor at pos as available.
  void ReturnProcessor(const CQLProcessorListPos& pos);

  // Allocate a prepared statement. If the statement already exists, return it instead.
  std::shared_ptr<CQLStatement> AllocatePreparedStatement(
      const CQLMessage::QueryId& id, const std::string& keyspace, const std::string& query);

  // Look up a prepared statement by its id. Nullptr will be returned if the statement is not found.
  std::shared_ptr<const CQLStatement> GetPreparedStatement(const CQLMessage::QueryId& id);

  std::shared_ptr<ql::Statement> GetAuthPreparedStatement() const { return auth_prepared_stmt_; }

  // Delete the prepared statement from the cache.
  void DeletePreparedStatement(const std::shared_ptr<const CQLStatement>& stmt);

  // Return the memory tracker for prepared statements.
  const MemTrackerPtr& prepared_stmts_mem_tracker() const {
    return prepared_stmts_mem_tracker_;
  }

  // Return the YBClient to communicate with either master or tserver.
  const std::shared_ptr<client::YBClient>& client() const;

  // Return the YBClientCache.
  const std::shared_ptr<client::YBMetaDataCache>& metadata_cache() const;

  // Return the CQL metrics.
  std::shared_ptr<CQLMetrics> cql_metrics() const { return cql_metrics_; }

  // Return the messenger.
  std::weak_ptr<rpc::Messenger> messenger() { return messenger_; }

  client::TransactionPool* GetTransactionPool();

  server::Clock* clock();

 private:
  constexpr static int kRpcTimeoutSec = 5;

  // Either gets an available processor or creates a new one.
  CQLProcessor *GetProcessor();

  // Insert a prepared statement at the front of the LRU list. "prepared_stmts_mutex_" needs to be
  // locked before this call.
  void InsertLruPreparedStatementUnlocked(const std::shared_ptr<CQLStatement>& stmt);

  // Move a prepared statement to the front of the LRU list. "prepared_stmts_mutex_" needs to be
  // locked before this call.
  void MoveLruPreparedStatementUnlocked(const std::shared_ptr<CQLStatement>& stmt);

  // Delete a prepared statement from the cache and the LRU list. "prepared_stmts_mutex_" needs to
  // be locked before this call.
  void DeletePreparedStatementUnlocked(const std::shared_ptr<const CQLStatement> stmt);

  // Delete the least recently used prepared statement from the cache to free up memory.
  void CollectGarbage(size_t required) override;

  // CQLServer of this service.
  CQLServer* const server_;

  // YBClient is to communicate with either master or tserver.
  yb::client::AsyncClientInitialiser async_client_init_;

  // A cache to reduce opening tables or (user-defined) types again and again.
  mutable std::shared_ptr<client::YBMetaDataCache> metadata_cache_;
  mutable std::atomic<bool> is_metadata_initialized_ = { false };
  mutable std::mutex metadata_init_mutex_;

  // List of CQL processors (in-use and available). In-use ones are at the beginning and available
  // ones at the end.
  CQLProcessorList processors_;

  // Next available CQL processor.
  CQLProcessorListPos next_available_processor_;

  // Mutex that protects access to processors_.
  std::mutex processors_mutex_;

  // Prepared statements cache.
  CQLStatementMap prepared_stmts_map_;

  // Prepared statements LRU list (least recently used one at the end).
  CQLStatementList prepared_stmts_list_;

  // Mutex that protects the prepared statements and the LRU list.
  std::mutex prepared_stmts_mutex_;

  std::shared_ptr<ql::Statement> auth_prepared_stmt_;

  // Tracker to measure and limit memory usage of prepared statements.
  MemTrackerPtr prepared_stmts_mem_tracker_;

  // Metrics to be collected and reported.
  yb::rpc::RpcMethodMetrics metrics_;

  std::shared_ptr<CQLMetrics> cql_metrics_;
  // Used to requeue the cql_inbound call to handle the response callback(s).
  std::weak_ptr<rpc::Messenger> messenger_;

  client::LocalTabletFilter local_tablet_filter_;

  std::atomic<client::TransactionPool*> transaction_pool_{nullptr};
  std::mutex transaction_pool_mutex_;
  std::unique_ptr<client::TransactionManager> transaction_manager_holder_;
  std::unique_ptr<client::TransactionPool> transaction_pool_holder_;
};

}  // namespace cqlserver
}  // namespace yb

#endif  // YB_YQL_CQL_CQLSERVER_CQL_SERVICE_H_
