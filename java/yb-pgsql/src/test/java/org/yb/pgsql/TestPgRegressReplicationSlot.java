// Copyright (c) YugabyteDB, Inc.
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
package org.yb.pgsql;

import java.util.Map;

import org.junit.Test;
import org.junit.runner.RunWith;
import org.yb.YBTestRunner;

/**
 * Runs the pg_regress replication_slot-related tests on YB code.
 */
@RunWith(value = YBTestRunner.class)
public class TestPgRegressReplicationSlot extends BasePgRegressTest {
  @Override
  public int getTestMethodTimeoutSec() {
    return 1800;
  }

  @Override
  protected Map<String, String> getTServerFlags() {
    Map<String, String> flagMap = super.getTServerFlags();

    if (isTestRunningWithConnectionManager()) {
      flagMap.put("allowed_preview_flags_csv",
          "ysql_yb_enable_replication_commands," +
          "yb_enable_cdc_consistent_snapshot_streams," +
          "ysql_yb_enable_replica_identity," +
          "enable_ysql_conn_mgr");
      flagMap.put("enable_ysql_conn_mgr", "true");
    } else {
      flagMap.put("allowed_preview_flags_csv",
          "ysql_yb_enable_replication_commands," +
          "yb_enable_cdc_consistent_snapshot_streams," +
          "ysql_yb_enable_replica_identity");
    }
    flagMap.put("ysql_yb_enable_replication_commands", "true");
    flagMap.put("yb_enable_cdc_consistent_snapshot_streams", "true");
    flagMap.put("ysql_TEST_enable_replication_slot_consumption", "true");
    flagMap.put("ysql_yb_enable_replica_identity", "true");
    flagMap.put(
        "vmodule", "cdc_service=4,cdcsdk_producer=4,ybc_pggate=4,cdcsdk_virtual_wal=4,client=4");
    return flagMap;
  }

  @Override
  protected Map<String, String> getMasterFlags() {
    Map<String, String> flagMap = super.getMasterFlags();
    flagMap.put("allowed_preview_flags_csv",
        "ysql_yb_enable_replication_commands," +
        "yb_enable_cdc_consistent_snapshot_streams," +
        "ysql_yb_enable_replica_identity");
    flagMap.put("ysql_yb_enable_replication_commands", "true");
    flagMap.put("yb_enable_cdc_consistent_snapshot_streams", "true");
    flagMap.put("ysql_TEST_enable_replication_slot_consumption", "true");
    flagMap.put("ysql_yb_enable_replica_identity", "true");
    return flagMap;
  }

  @Test
  public void testPgRegressReplicationSlot() throws Exception {
    runPgRegressTest("yb_replication_slot_schedule");
  }
}
