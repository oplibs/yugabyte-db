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

#include "yb/gen_yrpc/forward_generator.h"

#include "yb/gen_yrpc/model.h"

namespace yb {
namespace gen_yrpc {

namespace {

void MessageForward(YBPrinter printer, const google::protobuf::Descriptor* message) {
  for (auto i = 0; i != message->nested_type_count(); ++i) {
    MessageForward(printer, message->nested_type(i));
  }

  ScopedSubstituter message_substituter(printer, message);
  printer("class $message_name$;\n");
}

} // namespace

void ForwardGenerator::Header(YBPrinter printer, const google::protobuf::FileDescriptor* file) {
  printer(
      "// THIS FILE IS AUTOGENERATED FROM $path$\n"
      "\n"
      "#ifndef $upper_case$_MESSAGES_H\n"
      "#define $upper_case$_MESSAGES_H\n"
      "\n"
  );

  auto deps = ListDependencies(file);
  if (!deps.empty()) {
    for (const auto& dep : deps) {
      printer(
          "#include \"" + dep + ".fwd.h\"\n"
      );
    }
    printer("\n");
  }

  printer(
      "$open_namespace$\n"
  );

  for (int i = 0; i != file->message_type_count(); ++i) {
    MessageForward(printer, file->message_type(i));
  }

  printer(
      "\n$close_namespace$\n"
      "#endif // $upper_case$_MESSAGES_H\n"
  );
}

}  // namespace gen_yrpc
}  // namespace yb
