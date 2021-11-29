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
#include <gtest/gtest.h>

#include "yb/client/client.h"
#include "yb/client/schema.h"
#include "yb/client/table.h"
#include "yb/client/table_creator.h"
#include "yb/client/yb_table_name.h"
#include "yb/common/common.pb.h"
#include "yb/common/entity_ids.h"
#include "yb/consensus/consensus.pb.h"
#include "yb/consensus/consensus.proxy.h"
#include "yb/integration-tests/external_mini_cluster.h"
#include "yb/integration-tests/load_balancer_test_util.h"
#include "yb/integration-tests/mini_cluster.h"
#include "yb/integration-tests/yb_table_test_base.h"
#include "yb/tools/yb-admin_client.h"
#include "yb/util/monotime.h"
#include "yb/util/result.h"

using namespace std::literals;

namespace yb {
namespace integration_tests {

const auto kDefaultTimeout = 30000ms;
constexpr int kNumTables = 3;
constexpr int kMovesPerTable = 1;
constexpr int kNumTxnTablets = 6;

// We need multiple tables in order to test load_balancer_max_concurrent_moves_per_table.
class LoadBalancerColocatedTablesTest : public YBTableTestBase {
 protected:
  bool use_yb_admin_client() override { return true; }

  bool use_external_mini_cluster() override { return true; }

  bool enable_ysql() override {
    // Create the transaction status table.
    return true;
  }

  int num_tablets() override {
    return 5;
  }

  void CustomizeExternalMiniCluster(ExternalMiniClusterOptions* opts) override {
    opts->extra_tserver_flags.push_back("--placement_cloud=c");
    opts->extra_tserver_flags.push_back("--placement_region=r");
    opts->extra_tserver_flags.push_back("--placement_zone=z${index}");
    opts->extra_master_flags.push_back("--load_balancer_skip_leader_as_remove_victim=false");
    opts->extra_master_flags.push_back("--load_balancer_max_concurrent_moves=10");
    opts->extra_master_flags.push_back("--load_balancer_max_concurrent_moves_per_table="
                                       + std::to_string(kMovesPerTable));
    opts->extra_master_flags.push_back("--enable_global_load_balancing=true");
    // This value needs to be divisible by three so that the transaction tablets are evenly divided
    // amongst the three tservers that we end up creating.
    opts->extra_master_flags.push_back("--transaction_table_num_tablets="
                                       + std::to_string(kNumTxnTablets));
  }

  void CreateTables() {
    for (int i = 1; i <= kNumTables; ++i) {
      // Autogenerated ids will fail the IsPgsqlId() CHECKs so we need to generate oids.
      // Currently just using 1111, 2222, 3333, etc.
      const uint32_t db_oid = i * 1000 + i * 100 + i * 10 + i;
      const uint32_t table_oid = db_oid;
      table_names_.emplace_back(YQL_DATABASE_PGSQL,
                               GetPgsqlNamespaceId(db_oid),
                               "my_database-" + std::to_string(i),
                               GetPgsqlTableId(db_oid, table_oid),
                               "kv-table-test-" + std::to_string(i));
    }

    for (const auto& tn : table_names_) {
      ASSERT_OK(client_->CreateNamespaceIfNotExists(tn.namespace_name(),
                                                    tn.namespace_type(),
                                                    "",                 /* creator_role_name */
                                                    tn.namespace_id(),  /* namespace_id */
                                                    "",                 /* source_namespace_id */
                                                    boost::none,        /* next_pg_oid */
                                                    true                /* colocated */));

      client::YBSchemaBuilder b;
      b.AddColumn("k")->Type(BINARY)->NotNull()->PrimaryKey();
      b.AddColumn("v")->Type(BINARY)->NotNull();
      ASSERT_OK(b.Build(&schema_));

      ASSERT_OK(NewTableCreator()->table_name(tn)
                                  .table_id(tn.table_id())
                                  .schema(&schema_)
                                  .colocated(true)
                                  .Create());
    }
  }

  void DeleteTables() {
    for (const auto& tn : table_names_) {
      ASSERT_OK(client_->DeleteTable(tn));
    }
    table_names_.clear();
  }

  void CreateTable() override {
    if (!table_exists_) {
      CreateTables();
      table_exists_ = true;
    }
  }

  void DeleteTable() override {
    if (table_exists_) {
      DeleteTables();
      table_exists_ = false;
    }
  }

  // Modified to create SQL tables.
  std::unique_ptr<client::YBTableCreator> NewTableCreator() override {
    std::unique_ptr<client::YBTableCreator> table_creator(client_->NewTableCreator());
    if (num_tablets() > 0) {
      table_creator->num_tablets(num_tablets());
    }
    table_creator->table_type(client::YBTableType::PGSQL_TABLE_TYPE);
    return table_creator;
  }
};

// See issue #4871 about the disable in TSAN.
TEST_F(LoadBalancerColocatedTablesTest,
       YB_DISABLE_TEST_IN_TSAN(GlobalLoadBalancingWithColocatedTables)) {
  const int rf = 3;
  std::vector<uint32_t> z0_tserver_loads;
  // Start with 3 tables with 5 tablets.
  ASSERT_OK(yb_admin_client_->ModifyPlacementInfo("c.r.z0,c.r.z1,c.r.z2", rf, ""));

  // Add two tservers to z0 and wait for everything to be balanced (globally and per table).
  std::vector<std::string> extra_opts;
  extra_opts.push_back("--placement_cloud=c");
  extra_opts.push_back("--placement_region=r");
  extra_opts.push_back("--placement_zone=z0");
  ASSERT_OK(external_mini_cluster()->AddTabletServer(true, extra_opts));
  ASSERT_OK(external_mini_cluster()->AddTabletServer(true, extra_opts));
  ASSERT_OK(external_mini_cluster()->WaitForTabletServerCount(num_tablet_servers() + 2,
      kDefaultTimeout));

  // Wait for load balancing to complete.
  WaitForLoadBalanceCompletion();

  // Assert that each table is balanced, and that we are globally balanced.
  ASSERT_OK(client_->IsLoadBalanced(kNumTables * num_tablets() * rf));
  // Each colocated table should have its tablet on a different TS in z0.
  z0_tserver_loads = ASSERT_RESULT(GetTserverLoads({ 0, 3, 4 }));
  ASSERT_TRUE(AreLoadsBalanced(z0_tserver_loads));
  ASSERT_EQ(z0_tserver_loads[0], 1);
  ASSERT_EQ(z0_tserver_loads[1], 1);
  ASSERT_EQ(z0_tserver_loads[2], 1);
}

} // namespace integration_tests
} // namespace yb
