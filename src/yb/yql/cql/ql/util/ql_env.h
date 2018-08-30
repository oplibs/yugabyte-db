//--------------------------------------------------------------------------------------------------
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
// QLEnv defines the interface for the environment where SQL engine is running.
//
// If we support different types of servers underneath SQL engine (which we don't), this class
// should be an abstract interface and let the server (such as proxy server) defines the content.
//--------------------------------------------------------------------------------------------------

#ifndef YB_YQL_CQL_QL_UTIL_QL_ENV_H_
#define YB_YQL_CQL_QL_UTIL_QL_ENV_H_

#include "yb/client/callbacks.h"
#include "yb/client/client.h"
#include "yb/client/transaction.h"
#include "yb/client/transaction_manager.h"

#include "yb/gutil/callback.h"

#include "yb/rpc/rpc_fwd.h"

#include "yb/yql/cql/cqlserver/cql_rpc.h"
#include "yb/yql/cql/ql/ql_session.h"

#include "yb/common/common.pb.h"
#include "yb/util/enums.h"
#include "yb/server/hybrid_clock.h"

namespace yb {
namespace ql {

typedef std::function<client::TransactionManager*()> TransactionManagerProvider;

class QLEnv {
 public:
  //------------------------------------------------------------------------------------------------
  // Public types.
  typedef std::unique_ptr<QLEnv> UniPtr;
  typedef std::unique_ptr<const QLEnv> UniPtrConst;

  //------------------------------------------------------------------------------------------------
  // Constructor & destructor.
  QLEnv(std::shared_ptr<client::YBClient> client,
        std::shared_ptr<client::YBMetaDataCache> cache,
        const server::ClockPtr& clock,
        TransactionManagerProvider transaction_manager_provider);
  virtual ~QLEnv();

  //------------------------------------------------------------------------------------------------
  // Table related methods.

  virtual client::YBTableCreator *NewTableCreator();

  virtual client::YBTableAlterer *NewTableAlterer(const client::YBTableName& table_name);

  virtual CHECKED_STATUS TruncateTable(const std::string& table_id);

  virtual CHECKED_STATUS DeleteTable(const client::YBTableName& name);

  virtual CHECKED_STATUS DeleteIndexTable(const client::YBTableName& name,
                                          client::YBTableName* indexed_table_name);

  virtual std::shared_ptr<client::YBTable> GetTableDesc(const client::YBTableName& table_name,
                                                        bool *cache_used);
  virtual std::shared_ptr<client::YBTable> GetTableDesc(const TableId& table_id, bool *cache_used);

  virtual void RemoveCachedTableDesc(const client::YBTableName& table_name);
  virtual void RemoveCachedTableDesc(const TableId& table_id);

  //------------------------------------------------------------------------------------------------
  // Read/write related methods.

  // Create a read/write session.
  client::YBSessionPtr NewSession();

  // Create a new transaction.
  client::YBTransactionPtr NewTransaction(const client::YBTransactionPtr& transaction,
                                          IsolationLevel isolation_level);

  //------------------------------------------------------------------------------------------------
  // Permission related methods.

  // Grant/Revoke a permission with the given arguments.
  virtual CHECKED_STATUS GrantRevokePermission(GrantRevokeStatementType statement_type,
                                               const PermissionType& permission,
                                               const ResourceType& resource_type,
                                               const std::string& canonical_resource,
                                               const char* resource_name,
                                               const char* namespace_name,
                                               const std::string& role_name);

  //------------------------------------------------------------------------------------------------
  // Keyspace related methods.

  // Create a new keyspace with the given name.
  virtual CHECKED_STATUS CreateKeyspace(const std::string& keyspace_name);

  // Delete keyspace with the given name.
  virtual CHECKED_STATUS DeleteKeyspace(const std::string& keyspace_name);

  // Use keyspace with the given name.
  virtual CHECKED_STATUS UseKeyspace(const std::string& keyspace_name);

  virtual std::string CurrentKeyspace() const {
    return ql_session()->current_keyspace();
  }

  //------------------------------------------------------------------------------------------------
  // Role related methods.

  // Create role with the given arguments.
  CHECKED_STATUS CreateRole(const std::string& role_name,
                            const std::string& salted_hash,
                            const bool login, const bool superuser);

  // Alter an existing role with the given arguments.
  CHECKED_STATUS AlterRole(const std::string& role_name,
                           const boost::optional<std::string>& salted_hash,
                           const boost::optional<bool> login,
                           const boost::optional<bool> superuser);

  // Delete role by name.
  virtual CHECKED_STATUS DeleteRole(const std::string& role_name);

  CHECKED_STATUS GrantRevokeRole(GrantRevokeStatementType statement_type,
                                 const std::string& granted_role_name,
                                 const std::string& recipient_role_name);

  virtual std::string CurrentRoleName() const {
    return ql_session()->current_role_name();
  }

  //------------------------------------------------------------------------------------------------
  // (User-defined) Type related methods.

  // Create (user-defined) type with the given arguments.
  CHECKED_STATUS CreateUDType(const std::string &keyspace_name,
                              const std::string &type_name,
                              const std::vector<std::string> &field_names,
                              const std::vector<std::shared_ptr<QLType>> &field_types);

  // Delete (user-defined) type by name.
  virtual CHECKED_STATUS DeleteUDType(const std::string &keyspace_name,
                                      const std::string &type_name);

  // Retrieve (user-defined) type by name.
  std::shared_ptr<QLType> GetUDType(const std::string &keyspace_name,
                                    const std::string &type_name,
                                    bool *cache_used);

  virtual void RemoveCachedUDType(const std::string& keyspace_name, const std::string& type_name);

  //------------------------------------------------------------------------------------------------
  // QLSession related methods.

  void set_ql_session(const QLSession::SharedPtr& ql_session) {
    ql_session_ = ql_session;
  }
  const QLSession::SharedPtr& ql_session() const {
    if (!ql_session_) {
      ql_session_.reset(new QLSession());
    }
    return ql_session_;
  }

 private:
  //------------------------------------------------------------------------------------------------
  // Persistent attributes.

  // YBClient, an API that SQL engine uses to communicate with all servers.
  std::shared_ptr<client::YBClient> client_;

  // YBMetaDataCache, a cache to avoid creating a new table or type for each call.
  std::shared_ptr<client::YBMetaDataCache> metadata_cache_;

  // Server clock.
  const server::ClockPtr clock_;

  // Transaction manager to create distributed transactions.
  TransactionManagerProvider transaction_manager_provider_;
  client::TransactionManager* transaction_manager_ = nullptr;

  //------------------------------------------------------------------------------------------------
  // Transient attributes.
  // The following attributes are reset implicitly for every execution.

  // The QL session processing the statement.
  mutable QLSession::SharedPtr ql_session_;
};

}  // namespace ql
}  // namespace yb

#endif  // YB_YQL_CQL_QL_UTIL_QL_ENV_H_
