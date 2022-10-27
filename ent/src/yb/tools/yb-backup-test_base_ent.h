// Copyright (c) YugaByte, Inc.
//
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

#pragma once

#include "yb/client/table_handle.h"

#include "yb/tools/tools_test_utils.h"

#include "yb/yql/pgwrapper/pg_wrapper_test_base.h"

using std::string;
using std::unique_ptr;
using std::vector;

namespace yb {
namespace tools {

namespace helpers {
YB_DEFINE_ENUM(TableOp, (kKeepTable)(kDropTable)(kDropDB));

} // namespace helpers

class YBBackupTest : public pgwrapper::PgCommandTestBase {
 protected:
  YBBackupTest() : pgwrapper::PgCommandTestBase(false, false) {}

  void SetUp() override;

  string GetTempDir(const string& subdir);

  Status RunBackupCommand(const vector<string>& args);

  void RecreateDatabase(const string& db);

  Result<client::YBTableName> GetTableName(
      const string& table_name, const string& log_prefix, const string& ns = string());

  Result<string> GetTableId(
      const string& table_name, const string& log_prefix, const string& ns = string());

  Result<google::protobuf::RepeatedPtrField<yb::master::TabletLocationsPB>> GetTablets(
      const string& table_name, const string& log_prefix, const string& ns = string());

  bool CheckPartitions(
      const google::protobuf::RepeatedPtrField<yb::master::TabletLocationsPB>& tablets,
      const vector<string>& expected_splits);

  // Waiting for parent deletion is required if we plan to split the children created by this split
  // in the future.
  void ManualSplitTablet(
      const string& tablet_id, const string& table_name, const int expected_num_tablets,
      bool wait_for_parent_deletion, const std::string& namespace_name = string());

  void LogTabletsInfo(
      const google::protobuf::RepeatedPtrField<yb::master::TabletLocationsPB>& tablets);

  Status WaitForTabletFullyCompacted(size_t tserver_idx, const TabletId& tablet_id);

  void DoTestYEDISBackup(helpers::TableOp tableOp);
  void DoTestYSQLKeyspaceBackup(helpers::TableOp tableOp);
  void DoTestYSQLMultiSchemaKeyspaceBackup(helpers::TableOp tableOp);
  void DoTestYSQLKeyspaceWithHyphenBackupRestore(
      const string& backup_db, const string& restore_db);

  client::TableHandle table_;
  TmpDirProvider tmp_dir_;
};

}  // namespace tools
}  // namespace yb
