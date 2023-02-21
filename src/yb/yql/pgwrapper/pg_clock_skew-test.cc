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

#include <memory>
#include <string>

#include "yb/client/yb_table_name.h"
#include "yb/common/common.pb.h"
#include "yb/integration-tests/cql_test_util.h"
#include "yb/master/master_defaults.h"
#include "yb/server/skewed_clock.h"
#include "yb/tserver/mini_tablet_server.h"
#include "yb/tserver/tablet_server.h"
#include "yb/util/debug-util.h"
#include "yb/util/lw_function.h"
#include "yb/util/metrics.h"
#include "yb/util/status.h"
#include "yb/util/test_macros.h"

#include "yb/yql/pgwrapper/libpq_utils.h"
#include "yb/yql/pgwrapper/pg_mini_test_base.h"

DECLARE_bool(TEST_allow_skewed_clock_in_ysql);

namespace yb {
namespace pgwrapper {
namespace {

class PgClockSkewTest : public PgMiniTestBase {
 protected:
  void SetUp() override {
    FLAGS_TEST_allow_skewed_clock_in_ysql = true;
    server::SkewedClock::Register();
    PgMiniTestBase::SetUp();
  }

  size_t NumTabletServers() override {
    return 3;
  }

  std::vector<uint16_t> NodeClockSkewsMs() const override {
    return std::vector<uint16_t>({100, 200, 300});
  }
};

} // namespace

TEST_F(PgClockSkewTest, InTxnLimitBug) {
  // This test case is inspired from #15933 which is in turn caused due to #16034.
  //
  // In a nutshell, the bug arises in the following scenario:
  // 1) Assume there are 3 tservers N1, N2 and N3 with clock skews +100ms, +200ms and +300ms.
  // 2) The client connects to N1 and DELETEs a row which resides on N2. The write time of the
  //    DELETE is picked on N2, say it is X.
  // 3) The client then issues an INSERT of the same row and N1 picks an in_txn_limit using the
  //    current time for this operation. Refer ReadHybridTimePB for semantics of in_txn_limit. But Y
  //    might be less than X because N1's clock lags behind N2.
  // 4) This will result in a situation where the DELETE isn't visible to the INSERT. This will lead
  //    to a spurious duplicate key error.
  //
  // The situation where Y is less than X should occur even if clock skew exists. This is ensured by
  // propagation of hybrid times across nodes in all rpcs which guarantees hybrid time monotonicity
  // in a causal chains of events.
  auto conn = ASSERT_RESULT(Connect());
  ASSERT_OK(conn.Execute("CREATE TABLE test (k int primary key, v int)"));
  ASSERT_OK(conn.Execute("CREATE UNIQUE INDEX test_value_key1 ON test USING lsm (v HASH)"));
  ASSERT_OK(conn.Execute("INSERT INTO test SELECT s, s from generate_series(1, 10) as s"));
  for (size_t i = 1; i <= 10; ++i) {
    // This UPDATE will result in a DELETE + INSERT on the same index key v1=2, which would
    // trigger the bug seen in #15933 -- i.e., the INSERT wouldn't see the data written by the
    // DELETE and hence throw a duplicate key constraint violation.
    ASSERT_OK(conn.ExecuteFormat("UPDATE test SET v=$0 WHERE k = $0", i));
  }

  // The same bug as seen in #15933 can be seen on a main table with a simple DELETE + INSERT.
  ASSERT_OK(conn.Execute("DROP INDEX test_value_key1"));
  for (size_t i = 1; i <= 10; ++i) {
    ASSERT_OK(conn.Execute("BEGIN TRANSACTION ISOLATION LEVEL REPEATABLE READ"));
    ASSERT_OK(conn.ExecuteFormat("DELETE FROM test WHERE k = $0", i));
    ASSERT_OK(conn.ExecuteFormat("INSERT INTO test VALUES ($0, $0)", i));
    ASSERT_OK(conn.Execute("COMMIT"));
  }

  // Try an INSERT followed by INSERT to ensure that the second insert is able to see the first
  // and fails with a duplicate key error.
  ASSERT_OK(conn.Execute("TRUNCATE test"));
  for (size_t i = 1; i <= 10; ++i) {
    ASSERT_OK(conn.Execute("BEGIN TRANSACTION ISOLATION LEVEL REPEATABLE READ"));
    ASSERT_OK(conn.ExecuteFormat("INSERT INTO test VALUES ($0, $0)", i));
    ASSERT_NOK(conn.ExecuteFormat("INSERT INTO test VALUES ($0, $0)", i));
    ASSERT_OK(conn.Execute("ROLLBACK"));
  }
}

} // namespace pgwrapper
} // namespace yb
