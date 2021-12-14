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

#include "yb/gen_yrpc/proxy_generator.h"

#include <google/protobuf/descriptor.h>

#include "yb/gen_yrpc/metric_descriptor.h"
#include "yb/gen_yrpc/model.h"

namespace yb {
namespace gen_yrpc {

namespace {

std::vector<MetricDescriptor> outbound_metrics = {
  {
    .name = "request_bytes",
    .prefix = "proxy_",
    .kind = "counter",
    .extra_args = "",
    .units = "yb::MetricUnit::kBytes",
    .description = "Bytes sent by",
  },
  {
    .name = "response_bytes",
    .prefix = "proxy_",
    .kind = "counter",
    .extra_args = "",
    .units = "yb::MetricUnit::kBytes",
    .description = "Bytes received in response to",
  },
};

} // namespace

void ProxyGenerator::Header(YBPrinter printer, const google::protobuf::FileDescriptor *file) {
  printer(
      "// THIS FILE IS AUTOGENERATED FROM $path$\n"
      "\n"
      "#ifndef $upper_case$_PROXY_DOT_H\n"
      "#define $upper_case$_PROXY_DOT_H\n"
      "\n"
      "#include \"$path_no_extension$.pb.h\"\n"
  );

  if (HasLightweightMethod(file, rpc::RpcSides::PROXY)) {
    printer("#include \"$path_no_extension$.messages.h\"\n");
  }

  printer(
    "\n"
      "#include \"yb/rpc/proxy.h\"\n"
      "#include \"yb/util/status.h\"\n"
      "#include \"yb/util/net/net_fwd.h\"\n"
      "\n"
      "namespace yb {\n"
      "namespace rpc {\n"
      "class Proxy;\n"
      "}\n"
      "}\n"
      "\n"
      "$open_namespace$"
      "\n"
      "\n"
  );

  for (int service_idx = 0; service_idx < file->service_count(); ++service_idx) {
    const auto* service = file->service(service_idx);
    ScopedSubstituter service_subs(printer, service);

    printer(
        "class $service_name$Proxy : public ::yb::rpc::ProxyBase {\n"
        " public:\n"
        "  $service_name$Proxy(\n"
        "      ::yb::rpc::ProxyCache* cache, const ::yb::HostPort& remote,\n"
        "      const ::yb::rpc::Protocol* protocol = nullptr,\n"
        "      const ::yb::MonoDelta& resolve_cache_timeout = ::yb::MonoDelta());\n"
    );

    for (int method_idx = 0; method_idx < service->method_count(); ++method_idx) {
      ScopedSubstituter method_subs(printer, service->method(method_idx), rpc::RpcSides::PROXY);

      printer(
          "\n"
          "  ::yb::Status $rpc_name$(const $request$ &req, $response$ *resp,\n"
          "                          ::yb::rpc::RpcController *controller);\n"
          "  void $rpc_name$Async(const $request$ &req,\n"
          "                       $response$ *response,\n"
          "                       ::yb::rpc::RpcController *controller,\n"
          "                       ::yb::rpc::ResponseCallback callback);\n"
      );
    }

    printer("};\n\n");
  }

  printer(
      "$close_namespace$"
      "\n"
      "#endif // $upper_case$_PROXY_DOT_H\n"
  );
}

void ProxyGenerator::Source(YBPrinter printer, const google::protobuf::FileDescriptor *file) {
  printer(
      "// THIS FILE IS AUTOGENERATED FROM $path$\n"
      "\n"
      "#include \"$path_no_extension$.proxy.h\"\n"
      "\n"
      "#include \"$path_no_extension$.service.h\"\n\n"
      "#include \"yb/rpc/proxy.h\"\n"
      "#include \"yb/rpc/outbound_call.h\"\n"
      "#include \"yb/util/metrics.h\"\n"
      "#include \"yb/util/net/sockaddr.h\"\n"
      "\n"
  );

  GenerateMetricDefines(printer, file, outbound_metrics);

  printer(
      "$open_namespace$\n\n"
      "namespace {\n\n"
  );

  for (int service_idx = 0; service_idx < file->service_count(); ++service_idx) {
    const auto* service = file->service(service_idx);
    ScopedSubstituter service_subs(printer, service);
    printer("const std::string kFull$service_name$Name = \"$full_service_name$\";\n\n");

    printer(
        "::yb::rpc::ProxyMetricsPtr Create$service_name$Metrics("
            "const scoped_refptr<MetricEntity>& entity) {\n"
        "  auto result = std::make_shared<"
            "::yb::rpc::ProxyMetricsImpl<$service_method_count$>>();\n"
    );

    GenerateMethodAssignments(
        printer, service, "result->value[to_underlying($service_method_enum$::$metric_enum_key$)]",
        false, outbound_metrics);

    printer(
        "  return result;\n}\n\n"
    );
  }

  printer(
      "\n} // namespace\n\n"
  );

  for (int service_idx = 0; service_idx < file->service_count(); ++service_idx) {
    const auto* service = file->service(service_idx);
    ScopedSubstituter service_subs(printer, service);

    printer(
        "$service_name$Proxy::$service_name$Proxy(\n"
        "    ::yb::rpc::ProxyCache* cache, const ::yb::HostPort& remote,\n"
        "    const ::yb::rpc::Protocol* protocol,\n"
        "    const ::yb::MonoDelta& resolve_cache_timeout)\n"
        "    : ProxyBase(kFull$service_name$Name, &Create$service_name$Metrics,\n"
        "                cache, remote, protocol, resolve_cache_timeout) {}\n\n"
    );

    for (int method_idx = 0; method_idx < service->method_count(); ++method_idx) {
      ScopedSubstituter method_subs(printer, service->method(method_idx), rpc::RpcSides::PROXY);

      printer(
          "::yb::Status $service_name$Proxy::$rpc_name$(\n"
          "    const $request$ &req, $response$ *resp, ::yb::rpc::RpcController *controller) {\n"
          "  static ::yb::rpc::RemoteMethod method(\"$full_service_name$\", \"$rpc_name$\");\n"
          "  return proxy().SyncRequest(\n"
          "      &method, metrics<$service_method_count$>(static_cast<size_t>("
              "$service_method_enum$::$metric_enum_key$)), req, resp, controller);\n"
          "}\n"
          "\n"
          "void $service_name$Proxy::$rpc_name$Async(\n"
          "    const $request$ &req, $response$ *resp, ::yb::rpc::RpcController *controller,\n"
          "    ::yb::rpc::ResponseCallback callback) {\n"
          "  static ::yb::rpc::RemoteMethod method(\"$full_service_name$\", \"$rpc_name$\");\n"
          "  proxy().AsyncRequest(\n"
          "      &method, metrics<$service_method_count$>(static_cast<size_t>("
              "$service_method_enum$::$metric_enum_key$)), req, resp, controller, "
              "std::move(callback));\n"
          "}\n"
          "\n"
      );
    }
  }

  printer("$close_namespace$");
}

} // namespace gen_yrpc
} // namespace yb
