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

#ifndef YB_GEN_YRPC_MODEL_H
#define YB_GEN_YRPC_MODEL_H

#include <google/protobuf/wire_format_lite.h>

#include "yb/gen_yrpc/gen_yrpc_fwd.h"

#include "yb/util/strongly_typed_bool.h"

namespace yb {
namespace gen_yrpc {

google::protobuf::internal::WireFormatLite::FieldType FieldType(
    const google::protobuf::FieldDescriptor* field);
google::protobuf::internal::WireFormatLite::WireType WireType(
    const google::protobuf::FieldDescriptor* field);
size_t FixedSize(const google::protobuf::FieldDescriptor* field);
std::string ReplaceNamespaceDelimiters(const std::string& arg_full_name);
std::string RelativeClassPath(const std::string& clazz, const std::string& service);
std::string UnnestedName(
    const google::protobuf::Descriptor* message, bool full_path);
std::string MapFieldType(const google::protobuf::FieldDescriptor* field);
bool IsMessage(const google::protobuf::FieldDescriptor* field);
bool IsSimple(const google::protobuf::FieldDescriptor* field);

} // namespace gen_yrpc
} // namespace yb

#endif // YB_GEN_YRPC_MODEL_H
