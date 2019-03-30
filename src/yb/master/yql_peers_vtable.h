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

#ifndef YB_MASTER_YQL_PEERS_VTABLE_H
#define YB_MASTER_YQL_PEERS_VTABLE_H

#include "yb/master/yql_virtual_table.h"

#include "yb/util/net/net_fwd.h"

namespace yb {
namespace master {

// VTable implementation of system.peers.
class PeersVTable : public YQLVirtualTable {
 public:
  explicit PeersVTable(const Master* const master_);
  CHECKED_STATUS RetrieveData(const QLReadRequestPB& request,
                              std::unique_ptr<QLRowBlock>* vtable) const;

 private:
  Schema CreateSchema() const;

  std::unique_ptr<Resolver> resolver_;
};

}  // namespace master
}  // namespace yb
#endif // YB_MASTER_YQL_PEERS_VTABLE_H
