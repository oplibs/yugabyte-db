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

#include "yb/client/callbacks.h"
#include "yb/client/client.h"

#include "yb/consensus/log.h"
#include "yb/consensus/log_reader.h"

#include "yb/integration-tests/mini_cluster.h"
#include "yb/integration-tests/yb_table_test_base.h"

#include "yb/tablet/tablet_peer.h"

#include "yb/tserver/mini_tablet_server.h"
#include "yb/tserver/tablet_server.h"
#include "yb/tserver/ts_tablet_manager.h"

#include "yb/util/test_macros.h"
#include "yb/util/test_util.h"

DECLARE_bool(TEST_simulate_abrupt_server_restart);

namespace yb {
namespace integration_tests {

class RestartTest : public YBTableTestBase {
 protected:

  bool use_external_mini_cluster() override { return false; }

  int num_tablet_servers() override { return 3; }

  int num_tablets() override { return 1; }

  void GetTablet(const client::YBTableName& table_name, string* tablet_id) {
    std::vector<std::string> ranges;
    std::vector<TabletId> tablet_ids;
    ASSERT_OK(client_->GetTablets(table_name, 0 /* max_tablets */, &tablet_ids, &ranges));
    ASSERT_EQ(tablet_ids.size(), 1);
    *tablet_id = tablet_ids[0];
  }
};

TEST_F(RestartTest, WalFooterProperlyInitialized) {
  FLAGS_TEST_simulate_abrupt_server_restart = true;
  auto timestamp_before_write = GetCurrentTimeMicros();
  PutKeyValue("key", "value");
  auto timestamp_after_write = GetCurrentTimeMicros();

  auto* tablet_server = mini_cluster()->mini_tablet_server(0);
  ASSERT_OK(tablet_server->Restart());
  FLAGS_TEST_simulate_abrupt_server_restart = false;

  string tablet_id;
  ASSERT_NO_FATALS(GetTablet(table_.name(), &tablet_id));
  std::shared_ptr<tablet::TabletPeer> tablet_peer;
  ASSERT_OK(tablet_server->server()->tablet_manager()->GetTabletPeer(tablet_id, &tablet_peer));
  ASSERT_OK(tablet_server->WaitStarted());
  log::SegmentSequence segments;
  ASSERT_OK(tablet_peer->log()->GetLogReader()->GetSegmentsSnapshot(&segments));

  ASSERT_EQ(2, segments.size());
  auto segment = segments[0];
  ASSERT_TRUE(segment->HasFooter());
  ASSERT_TRUE(segment->footer().has_close_timestamp_micros());
  ASSERT_TRUE(segment->footer().close_timestamp_micros() > timestamp_before_write &&
              segment->footer().close_timestamp_micros() < timestamp_after_write);

}

} // namespace integration_tests
} // namespace yb
