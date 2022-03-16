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
// Helpers for dealing with the protobufs defined in wire_protocol.proto.
#ifndef YB_COMMON_WIRE_PROTOCOL_H
#define YB_COMMON_WIRE_PROTOCOL_H

#include <vector>

#include "yb/common/common_fwd.h"

#include <google/protobuf/repeated_field.h>

#include "yb/gutil/endian.h"

#include "yb/util/status_fwd.h"
#include "yb/util/cast.h"
#include "yb/util/enums.h"
#include "yb/util/math_util.h"
#include "yb/util/net/net_fwd.h"
#include "yb/util/status_ec.h"
#include "yb/util/type_traits.h"
#include "yb/util/result.h"

namespace yb {

class ColumnId;
class ColumnSchema;
class faststring;
class HostPort;
class RowChangeList;
class Schema;
class Slice;

// Convert the given C++ Status object into the equivalent Protobuf.
void StatusToPB(const Status& status, AppStatusPB* pb);

// Convert the given protobuf into the equivalent C++ Status object.
Status StatusFromPB(const AppStatusPB& pb);

// Convert the specified HostPort to protobuf.
void HostPortToPB(const HostPort& host_port, HostPortPB* host_port_pb);

// Returns the HostPort created from the specified protobuf.
HostPort HostPortFromPB(const HostPortPB& host_port_pb);

bool HasHostPortPB(
    const google::protobuf::RepeatedPtrField<HostPortPB>& list, const HostPortPB& hp);

// Returns an Endpoint from HostPortPB.
CHECKED_STATUS EndpointFromHostPortPB(const HostPortPB& host_portpb, Endpoint* endpoint);

// Adds addresses in 'addrs' to 'pbs'. If an address is a wildcard (e.g., "0.0.0.0"),
// then the local machine's FQDN or its network interface address is used in its place.
CHECKED_STATUS AddHostPortPBs(const std::vector<Endpoint>& addrs,
                              google::protobuf::RepeatedPtrField<HostPortPB>* pbs);

// Simply convert the list of host ports into a repeated list of corresponding PB's.
void HostPortsToPBs(const std::vector<HostPort>& addrs,
                    google::protobuf::RepeatedPtrField<HostPortPB>* pbs);

// Convert list of HostPortPBs into host ports.
void HostPortsFromPBs(const google::protobuf::RepeatedPtrField<HostPortPB>& pbs,
                      std::vector<HostPort>* addrs);

enum SchemaPBConversionFlags {
  SCHEMA_PB_WITHOUT_IDS = 1 << 0,
};

// Convert the specified schema to protobuf.
// 'flags' is a bitfield of SchemaPBConversionFlags values.
void SchemaToPB(const Schema& schema, SchemaPB* pb, int flags = 0);

// Convert the specified schema to protobuf without column IDs.
void SchemaToPBWithoutIds(const Schema& schema, SchemaPB *pb);

// Returns the Schema created from the specified protobuf.
// If the schema is invalid, return a non-OK status.
Status SchemaFromPB(const SchemaPB& pb, Schema *schema);

// Convert the specified column schema to protobuf.
// 'flags' is a bitfield of SchemaPBConversionFlags values.
void ColumnSchemaToPB(const ColumnSchema& schema, ColumnSchemaPB *pb, int flags = 0);

// Return the ColumnSchema created from the specified protobuf.
ColumnSchema ColumnSchemaFromPB(const ColumnSchemaPB& pb);

// Convert the given list of ColumnSchemaPB objects into a Schema object.
//
// Returns InvalidArgument if the provided columns don't make a valid Schema
// (eg if the keys are non-contiguous or nullable).
Status ColumnPBsToSchema(
  const google::protobuf::RepeatedPtrField<ColumnSchemaPB>& column_pbs,
  Schema* schema);

// Returns the required information from column pbs to build the column part of SchemaPB.
CHECKED_STATUS ColumnPBsToColumnTuple(
    const google::protobuf::RepeatedPtrField<ColumnSchemaPB>& column_pbs,
    std::vector<ColumnSchema>* columns , std::vector<ColumnId>* column_ids, int* num_key_columns);

// Extract the columns of the given Schema into protobuf objects.
//
// The 'cols' list is replaced by this method.
// 'flags' is a bitfield of SchemaPBConversionFlags values.
void SchemaToColumnPBs(
  const Schema& schema,
  google::protobuf::RepeatedPtrField<ColumnSchemaPB>* cols,
  int flags = 0);

// Extract the colocated table information of the given schema into a protobuf object.
void SchemaToColocatedTableIdentifierPB(
    const Schema& schema, ColocatedTableIdentifierPB* colocated_pb);

YB_DEFINE_ENUM(UsePrivateIpMode, (cloud)(region)(zone)(never));

// Returns mode for selecting between private and public IP.
Result<UsePrivateIpMode> GetPrivateIpMode();

// Pick node's public host and port
// registration - node registration information
const HostPortPB& PublicHostPort(const ServerRegistrationPB& registration);

// Pick host and port that should be used to connect node
// broadcast_addresses - node public host ports
// private_host_ports - node private host ports
// connect_to - node placement information
// connect_from - placement information of connect originator
const HostPortPB& DesiredHostPort(
    const google::protobuf::RepeatedPtrField<HostPortPB>& broadcast_addresses,
    const google::protobuf::RepeatedPtrField<HostPortPB>& private_host_ports,
    const CloudInfoPB& connect_to,
    const CloudInfoPB& connect_from);

// Pick host and port that should be used to connect node
// registration - node registration information
// connect_from - placement information of connect originator
const HostPortPB& DesiredHostPort(
    const ServerRegistrationPB& registration, const CloudInfoPB& connect_from);

HAS_MEMBER_FUNCTION(error);
HAS_MEMBER_FUNCTION(status);

template<class Response>
CHECKED_STATUS ResponseStatus(
    const Response& response,
    typename std::enable_if<HasMemberFunction_error<Response>::value, void*>::type = nullptr) {
  // Response has has_error method, use status from it.
  if (response.has_error()) {
    return StatusFromPB(response.error().status());
  }
  return Status::OK();
}

template<class Response>
CHECKED_STATUS ResponseStatus(
    const Response& response,
    typename std::enable_if<HasMemberFunction_status<Response>::value &&
                            !HasMemberFunction_error<Response>::value, void*>::type = nullptr) {
  if (response.has_status()) {
    return StatusFromPB(response.status());
  }
  return Status::OK();
}

struct SplitChildTabletIdsTag : yb::StringVectorBackedErrorTag {
  // It is part of the wire protocol and should not be changed once released.
  static constexpr uint8_t kCategory = 14;

  static std::string ToMessage(Value value);
};

typedef yb::StatusErrorCodeImpl<SplitChildTabletIdsTag> SplitChildTabletIdsData;

} // namespace yb

#endif  // YB_COMMON_WIRE_PROTOCOL_H
