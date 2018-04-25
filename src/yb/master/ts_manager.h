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
#ifndef YB_MASTER_TS_MANAGER_H
#define YB_MASTER_TS_MANAGER_H

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "yb/common/common.pb.h"
#include "yb/gutil/macros.h"
#include "yb/util/locks.h"
#include "yb/util/monotime.h"
#include "yb/util/status.h"

namespace yb {

class NodeInstancePB;

namespace master {

class TSDescriptor;
class TSRegistrationPB;

using TSDescSharedPtr = std::shared_ptr<TSDescriptor>;
using TSDescriptorVector = std::vector<TSDescSharedPtr>;
typedef std::string TabletServerId;

// Tracks the servers that the master has heard from, along with their
// last heartbeat, etc.
//
// Note that TSDescriptors are never deleted, even if the TS crashes
// and has not heartbeated in quite a while. This makes it simpler to
// keep references to TSDescriptors elsewhere in the master without
// fear of lifecycle problems. Dead servers are "dead, but not forgotten"
// (they live on in the heart of the master).
//
// This class is thread-safe.
class TSManager {
 public:
  TSManager();
  virtual ~TSManager();

  // Lookup the tablet server descriptor for the given instance identifier.
  // If the TS has never registered, or this instance doesn't match the
  // current instance ID for the TS, then a NotFound status is returned.
  // Otherwise, *desc is set and OK is returned.
  CHECKED_STATUS LookupTS(const NodeInstancePB& instance,
                          TSDescSharedPtr* desc);

  // Lookup the tablet server descriptor for the given UUID.
  // Returns false if the TS has never registered.
  // Otherwise, *desc is set and returns true.
  bool LookupTSByUUID(const std::string& uuid,
                      TSDescSharedPtr* desc);

  // Register or re-register a tablet server with the manager.
  //
  // If successful, *desc reset to the registered descriptor.
  CHECKED_STATUS RegisterTS(const NodeInstancePB& instance,
                            const TSRegistrationPB& registration,
                            TSDescSharedPtr* desc);

  // Return all of the currently registered TS descriptors into the provided list.
  void GetAllDescriptors(TSDescriptorVector* descs) const;

  // Return all of the currently registered TS descriptors that have sent a
  // heartbeat recently, indicating that they're alive and well.
  void GetAllLiveDescriptors(TSDescriptorVector* descs) const;

  // Return all of the currently registered TS descriptors that have sent a heartbeat
  // recently and are in the same 'cluster' with given placement uuid.
  void GetAllLiveDescriptorsInCluster(TSDescriptorVector* descs, string placement_uuid) const;

  // Return all of the currently registered TS descriptors that have sent a
  // heartbeat, indicating that they're alive and well, recently and have given
  // full report of their tablets as well.
  void GetAllReportedDescriptors(TSDescriptorVector* descs) const;

  // Get the TS count.
  int GetCount() const;

  // Return the tablet server descriptor running on the given port.
  const TSDescSharedPtr GetTSDescriptor(const HostPortPB& host_port) const;

  static bool IsTSLive(const TSDescSharedPtr& ts);

  // Check if the placement uuid of the tserver is same as given cluster uuid.
  static bool IsTsInCluster(const TSDescSharedPtr& ts, string cluster_uuid);

 private:

  void GetDescriptors(std::function<bool(const TSDescSharedPtr&)> condition,
                      TSDescriptorVector* descs) const;

  mutable rw_spinlock lock_;

  typedef std::unordered_map<std::string, TSDescSharedPtr> TSDescriptorMap;
  TSDescriptorMap servers_by_id_;

  DISALLOW_COPY_AND_ASSIGN(TSManager);
};

} // namespace master
} // namespace yb

#endif // YB_MASTER_TS_MANAGER_H
