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

#ifndef YB_YQL_CQL_QL_EXEC_EXEC_FWD_H
#define YB_YQL_CQL_QL_EXEC_EXEC_FWD_H

#include <vector>

#include "yb/yql/cql/ql/ptree/ptree_fwd.h"
#include "yb/yql/cql/ql/util/util_fwd.h"

namespace yb {
namespace ql {

class ExecContext;
class QueryPagingState;
class Rescheduler;
class TnodeContext;

// A batch of statement parse trees to execute with the parameters.
using StatementBatch = std::vector<std::pair<std::reference_wrapper<const ParseTree>,
                                             std::reference_wrapper<const StatementParameters>>>;

}  // namespace ql
}  // namespace yb

#endif  // YB_YQL_CQL_QL_EXEC_EXEC_FWD_H
