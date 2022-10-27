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

#include <string>
#include <unordered_set>
#include <vector>
#include <boost/algorithm/string/replace.hpp>
#include "yb/gutil/map-util.h"
#include "yb/gutil/strings/split.h"

#ifdef TCMALLOC_ENABLED
#include <gperftools/heap-profiler.h>
#endif

#include <boost/algorithm/string/case_conv.hpp>
#include "yb/gutil/strings/join.h"
#include "yb/gutil/strings/substitute.h"
#include "yb/util/auto_flags_util.h"
#include "yb/util/flags.h"
#include "yb/util/metrics.h"
#include "yb/util/path_util.h"
#include "yb/util/url-coding.h"
#include "yb/util/version_info.h"

using google::CommandLineFlagInfo;
using std::cout;
using std::endl;
using std::string;
using std::unordered_set;
using std::vector;

// Because every binary initializes its flags here, we use it as a convenient place
// to offer some global flags as well.
DEFINE_bool(dump_metrics_json, false,
            "Dump a JSON document describing all of the metrics which may be emitted "
            "by this binary.");
TAG_FLAG(dump_metrics_json, hidden);

DEFINE_bool(enable_process_lifetime_heap_profiling, false, "Enables heap "
    "profiling for the lifetime of the process. Profile output will be stored in the "
    "directory specified by -heap_profile_path. Enabling this option will disable the "
    "on-demand/remote server profile handlers.");
TAG_FLAG(enable_process_lifetime_heap_profiling, stable);
TAG_FLAG(enable_process_lifetime_heap_profiling, advanced);

DEFINE_string(heap_profile_path, "", "Output path to store heap profiles. If not set " \
    "profiles are stored in /tmp/<process-name>.<pid>.<n>.heap.");
TAG_FLAG(heap_profile_path, stable);
TAG_FLAG(heap_profile_path, advanced);

DEFINE_int32(svc_queue_length_default, 50, "Default RPC queue length for a service");
TAG_FLAG(svc_queue_length_default, advanced);

// This provides a more accurate representation of default gFlag values for application like
// yb-master which override the hard coded values at process startup time.
DEFINE_bool(
    dump_flags_xml, false,
    "Dump a XLM document describing all of gFlags used in this binary. Differs from helpxml by "
    "displaying the current runtime value as the default instead of the hard coded values from the "
    "flag definitions. ");
TAG_FLAG(dump_flags_xml, stable);
TAG_FLAG(dump_flags_xml, advanced);

DEFINE_bool(help_auto_flag_json, false,
    "Dump a JSON document describing all of the AutoFlags available in this binary.");
TAG_FLAG(help_auto_flag_json, stable);
TAG_FLAG(help_auto_flag_json, advanced);

DECLARE_bool(TEST_promote_all_auto_flags);

// Tag a bunch of the flags that we inherit from glog/gflags.

//------------------------------------------------------------
// GLog flags
//------------------------------------------------------------
// Most of these are considered stable. The ones related to email are
// marked unsafe because sending email inline from a server is a pretty
// bad idea.
DECLARE_string(alsologtoemail);
TAG_FLAG(alsologtoemail, hidden);
TAG_FLAG(alsologtoemail, unsafe);

// --alsologtostderr is deprecated in favor of --stderrthreshold
DECLARE_bool(alsologtostderr);
TAG_FLAG(alsologtostderr, hidden);
TAG_FLAG(alsologtostderr, runtime);

DECLARE_bool(colorlogtostderr);
TAG_FLAG(colorlogtostderr, stable);
TAG_FLAG(colorlogtostderr, runtime);

DECLARE_bool(drop_log_memory);
TAG_FLAG(drop_log_memory, advanced);
TAG_FLAG(drop_log_memory, runtime);

DECLARE_string(log_backtrace_at);
TAG_FLAG(log_backtrace_at, advanced);

DECLARE_string(log_dir);
TAG_FLAG(log_dir, stable);

DECLARE_string(log_link);
TAG_FLAG(log_link, stable);
TAG_FLAG(log_link, advanced);

DECLARE_bool(log_prefix);
TAG_FLAG(log_prefix, stable);
TAG_FLAG(log_prefix, advanced);
TAG_FLAG(log_prefix, runtime);

DECLARE_int32(logbuflevel);
TAG_FLAG(logbuflevel, advanced);
TAG_FLAG(logbuflevel, runtime);
DECLARE_int32(logbufsecs);
TAG_FLAG(logbufsecs, advanced);
TAG_FLAG(logbufsecs, runtime);

DECLARE_int32(logemaillevel);
TAG_FLAG(logemaillevel, hidden);
TAG_FLAG(logemaillevel, unsafe);

DECLARE_string(logmailer);
TAG_FLAG(logmailer, hidden);

DECLARE_bool(logtostderr);
TAG_FLAG(logtostderr, stable);
TAG_FLAG(logtostderr, runtime);

DECLARE_int32(max_log_size);
TAG_FLAG(max_log_size, stable);
TAG_FLAG(max_log_size, runtime);

DECLARE_int32(minloglevel);
TAG_FLAG(minloglevel, stable);
TAG_FLAG(minloglevel, advanced);
TAG_FLAG(minloglevel, runtime);

DECLARE_int32(stderrthreshold);
TAG_FLAG(stderrthreshold, stable);
TAG_FLAG(stderrthreshold, advanced);
TAG_FLAG(stderrthreshold, runtime);

DECLARE_bool(stop_logging_if_full_disk);
TAG_FLAG(stop_logging_if_full_disk, stable);
TAG_FLAG(stop_logging_if_full_disk, advanced);
TAG_FLAG(stop_logging_if_full_disk, runtime);

DECLARE_int32(v);
TAG_FLAG(v, stable);
TAG_FLAG(v, advanced);
TAG_FLAG(v, runtime);

DECLARE_string(vmodule);
TAG_FLAG(vmodule, stable);
TAG_FLAG(vmodule, runtime);
TAG_FLAG(vmodule, advanced);
namespace yb {
bool ValidateVmodule(const char* flag_name, const string& new_value);
void UpdateVmodule();
}  // namespace yb
DEFINE_validator(vmodule, &yb::ValidateVmodule);
REGISTER_CALLBACK(vmodule, "UpdateVmodule", &yb::UpdateVmodule);

DECLARE_bool(symbolize_stacktrace);
TAG_FLAG(symbolize_stacktrace, stable);
TAG_FLAG(symbolize_stacktrace, runtime);
TAG_FLAG(symbolize_stacktrace, advanced);

//------------------------------------------------------------
// GFlags flags
//------------------------------------------------------------
DECLARE_string(flagfile);
TAG_FLAG(flagfile, stable);

DECLARE_string(fromenv);
TAG_FLAG(fromenv, stable);
TAG_FLAG(fromenv, advanced);

DECLARE_string(tryfromenv);
TAG_FLAG(tryfromenv, stable);
TAG_FLAG(tryfromenv, advanced);

DECLARE_string(undefok);
TAG_FLAG(undefok, stable);
TAG_FLAG(undefok, advanced);

DECLARE_int32(tab_completion_columns);
TAG_FLAG(tab_completion_columns, stable);
TAG_FLAG(tab_completion_columns, hidden);

DECLARE_string(tab_completion_word);
TAG_FLAG(tab_completion_word, stable);
TAG_FLAG(tab_completion_word, hidden);

DECLARE_bool(help);
TAG_FLAG(help, stable);

DECLARE_bool(helpfull);
// We hide -helpfull because it's the same as -help for now.
TAG_FLAG(helpfull, stable);
TAG_FLAG(helpfull, hidden);

DECLARE_string(helpmatch);
TAG_FLAG(helpmatch, stable);
TAG_FLAG(helpmatch, advanced);

DECLARE_string(helpon);
TAG_FLAG(helpon, stable);
TAG_FLAG(helpon, advanced);

DECLARE_bool(helppackage);
TAG_FLAG(helppackage, stable);
TAG_FLAG(helppackage, advanced);

DECLARE_bool(helpshort);
TAG_FLAG(helpshort, stable);
TAG_FLAG(helpshort, advanced);

DECLARE_bool(helpxml);
TAG_FLAG(helpxml, stable);
TAG_FLAG(helpxml, advanced);

DECLARE_bool(version);
TAG_FLAG(version, stable);

DEFINE_string(
    dynamically_linked_exe_suffix, "",
    "Suffix to appended to executable names, such as yb-master and yb-tserver during the "
    "generation of Link Time Optimized builds.");
TAG_FLAG(dynamically_linked_exe_suffix, advanced);
TAG_FLAG(dynamically_linked_exe_suffix, hidden);
TAG_FLAG(dynamically_linked_exe_suffix, unsafe);

namespace yb {

// In LTO builds we first generate executable like yb-master and yb-tserver with a suffix
// ("-dynamic") added to their names. These executables are then optimized to produce the
// final executable without the suffix. Certain build targets like gen_flags_metadata and
// gen_auto_flags are run using the dynamic executables to generate metadata files which should
// contain final the program name.
string GetStaticProgramName() {
  auto program_name = BaseName(google::ProgramInvocationShortName());
  if (PREDICT_FALSE(
          !FLAGS_dynamically_linked_exe_suffix.empty() &&
          program_name.ends_with(FLAGS_dynamically_linked_exe_suffix))) {
    boost::replace_last(program_name, FLAGS_dynamically_linked_exe_suffix, "");
  }
  return program_name;
}

namespace {

void AppendXMLTag(const char* tag, const string& txt, string* r) {
  strings::SubstituteAndAppend(r, "<$0>$1</$0>", tag, EscapeForHtmlToString(txt));
}

YB_STRONGLY_TYPED_BOOL(OnlyDisplayDefaultFlagValue);

static string DescribeOneFlagInXML(
    const CommandLineFlagInfo& flag, OnlyDisplayDefaultFlagValue only_display_default_values) {
  unordered_set<FlagTag> tags;
  GetFlagTags(flag.name, &tags);
  // TODO(#14400): Until we make gflags string modifications atomic, we should not be making
  // runtime changes to string flags. However, to not have to do two rounds of auditing, we continue
  // to mark flags as runtime, regardless of their data type, purely based on whether they can
  // logically be modified at runtime.
  //
  // To keep external clients oblivious to this, we strip the runtime tag here, so to external
  // metadata, tooling and Platform automation, all string flags will be treated explicitly as not
  // runtime!
  if (flag.type == "string") {
    auto runtime_it = tags.find(FlagTag::kRuntime);
    if (runtime_it != tags.end()) {
      tags.erase(runtime_it);
    }
  }

  if (only_display_default_values && tags.contains(FlagTag::kHidden)) {
    return {};
  }

  vector<string> tags_str;
  std::transform(tags.begin(), tags.end(), std::back_inserter(tags_str), [](const FlagTag tag) {
    // Convert "kEnum_val" to "enum_val"
    auto name = ToString(tag).erase(0, 1);
    boost::algorithm::to_lower(name);
    return name;
  });

  auto auto_flag_desc = GetAutoFlagDescription(flag.name);

  string r("<flag>");
  AppendXMLTag("file", flag.filename, &r);
  AppendXMLTag("name", flag.name, &r);
  AppendXMLTag("meaning", flag.description, &r);

  if (auto_flag_desc) {
    AppendXMLTag("class", ToString(auto_flag_desc->flag_class), &r);
    if (!only_display_default_values) {
      AppendXMLTag(
          "state", IsFlagPromoted(flag, *auto_flag_desc) ? "promoted" : "not-promoted", &r);
    }
    AppendXMLTag("initial", auto_flag_desc->initial_val, &r);
    AppendXMLTag("target", auto_flag_desc->target_val, &r);
  } else {
    AppendXMLTag("default", flag.default_value, &r);
  }

  if (!only_display_default_values) {
    AppendXMLTag("current", flag.current_value, &r);
  }

  AppendXMLTag("type", flag.type, &r);
  AppendXMLTag("tags", JoinStrings(tags_str, ","), &r);
  r += "</flag>";
  return r;
}

namespace {

struct sort_flags_by_name {
  inline bool operator()(const CommandLineFlagInfo& flag1, const CommandLineFlagInfo& flag2) {
    const auto& a = flag1.name;
    const auto& b = flag2.name;
    for (size_t i = 0; i < a.size() && i < b.size(); i++) {
      if (std::tolower(a[i]) != std::tolower(b[i]))
        return (std::tolower(a[i]) < std::tolower(b[i]));
    }
    return a.size() < b.size();
  }
};
}  // namespace

void DumpFlagsXMLAndExit(OnlyDisplayDefaultFlagValue only_display_default_values) {
  vector<CommandLineFlagInfo> flags;
  GetAllFlags(&flags);

  cout << "<?xml version=\"1.0\"?>" << endl;
  cout << "<AllFlags>" << endl;
  cout << strings::Substitute(
              "<program>$0</program>", EscapeForHtmlToString(GetStaticProgramName()))
       << endl;
  cout << strings::Substitute(
      "<usage>$0</usage>",
      EscapeForHtmlToString(google::ProgramUsage())) << endl;

  std::sort(flags.begin(), flags.end(), sort_flags_by_name());

  for (const CommandLineFlagInfo& flag : flags) {
    const auto flag_info = DescribeOneFlagInXML(flag, only_display_default_values);
    if (!flag_info.empty()) {
      cout << flag_info << std::endl;
    }
  }

  cout << "</AllFlags>" << endl;
  exit(0);
}

void ShowVersionAndExit() {
  cout << VersionInfo::GetShortVersionString() << endl;
  exit(0);
}

void DumpAutoFlagsJSONAndExit() {
  // Promote all AutoFlags to ensure the target value passes any flag validation functions. Its ok
  // if the current values change as we don't print them out.
  auto status = PromoteAllAutoFlags();
  if (!status.ok()) {
    LOG(FATAL) << "Failed to promote all AutoFlags: " << status.ToString();
  }

  cout << AutoFlagsUtil::DumpAutoFlagsToJSON(GetStaticProgramName());
  exit(0);
}

void SetFlagDefaultsToCurrent(const std::vector<google::CommandLineFlagInfo>& flag_infos) {
  for (const auto& flag_info : flag_infos) {
    if (!flag_info.is_default) {
      // This is not expected to fail as we are setting default to the already validated current
      // value.
      CHECK(!gflags::SetCommandLineOptionWithMode(
                 flag_info.name.c_str(),
                 flag_info.current_value.c_str(),
                 gflags::FlagSettingMode::SET_FLAGS_DEFAULT)
                 .empty());
    }
  }
}

void InvokeAllCallbacks(const std::vector<google::CommandLineFlagInfo>& flag_infos) {
  for (const auto& flag_info : flag_infos) {
    flags_callback_internal::InvokeCallbacks(flag_info.flag_ptr, flag_info.name);
  }
}

}  // anonymous namespace

void ParseCommandLineFlags(int* argc, char*** argv, bool remove_flags) {
  {
    std::vector<google::CommandLineFlagInfo> flag_infos;
    google::GetAllFlags(&flag_infos);

    // gFlags have one hard-coded static default value in all programs that include the file
    // where it was defined. Programs that need custom defaults set the flag at runtime before the
    // call to ParseCommandLineFlags. So the current value is technically the default value used
    // by this program.
    SetFlagDefaultsToCurrent(flag_infos);

    google::ParseCommandLineNonHelpFlags(argc, argv, remove_flags);
    InvokeAllCallbacks(flag_infos);

    // flag_infos is no longer valid as default and current values have changed.
  }

  if (FLAGS_TEST_promote_all_auto_flags) {
    CHECK_OK(PromoteAllAutoFlags());
  }

  if (FLAGS_helpxml) {
    DumpFlagsXMLAndExit(OnlyDisplayDefaultFlagValue::kFalse);
  } else if (FLAGS_dump_flags_xml) {
    DumpFlagsXMLAndExit(OnlyDisplayDefaultFlagValue::kTrue);
  } else if (FLAGS_help_auto_flag_json) {
    DumpAutoFlagsJSONAndExit();
  } else if (FLAGS_dump_metrics_json) {
    std::stringstream s;
    JsonWriter w(&s, JsonWriter::PRETTY);
    WriteRegistryAsJson(&w);
    std::cout << s.str() << std::endl;
    exit(0);
  } else if (FLAGS_version) {
    ShowVersionAndExit();
  } else {
    google::HandleCommandLineHelpFlags();
  }

  if (FLAGS_heap_profile_path.empty()) {
    const auto path =
        strings::Substitute("/tmp/$0.$1", google::ProgramInvocationShortName(), getpid());
    CHECK_OK(SET_FLAG_DEFAULT_AND_CURRENT(heap_profile_path, path));
  }

#ifdef TCMALLOC_ENABLED
  if (FLAGS_enable_process_lifetime_heap_profiling) {
    HeapProfilerStart(FLAGS_heap_profile_path.c_str());
  }
#endif
}

bool RefreshFlagsFile(const std::string& filename) {
  // prog_name is a placeholder that isn't really used by ReadFromFlags.
  // TODO: Find a better way to refresh flags from the file, ReadFromFlagsFile is going to be
  // deprecated.
  const char* prog_name = "yb";
  if (!google::ReadFromFlagsFile(filename, prog_name, false /* errors_are_fatal */)) {
    return false;
  }

  if (FLAGS_TEST_promote_all_auto_flags) {
    CHECK_OK(PromoteAllAutoFlags());
  }

  return true;
}

// Validates that the requested updates to vmodule can be made.
bool ValidateVmodule(const char* flag_name, const string& new_value) {
  auto requested_settings = strings::Split(new_value, ",");
  for (const auto& module_value : requested_settings) {
    if (module_value.empty()) {
      continue;
    }
    vector<string> kv = strings::Split(module_value, "=");
    if (kv.size() != 2 || kv[0].empty() || kv[1].empty()) {
      LOG(ERROR) << Format(
          "'$0' is not valid. vmodule should be a comma list of <module_pattern>=<logging_level>",
          module_value);
      return false;
    }

    char* end;
    errno = 0;
    const long value = strtol(kv[1].c_str(), &end, 10);
    if (*end != '\0' || errno == ERANGE || value > INT_MAX || value < INT_MIN) {
      LOG(ERROR) << Format(
          "'$0' is not a valid integer number. Cannot update vmodule setting for module '$1'",
          kv[1], kv[0]);
      return false;
    }
  }

  return true;
}

namespace {
std::mutex vmodule_mtx;
// Use a vector instead of map as order matters. For each file the first matching pattern is applied
// and it is not updated even if a better matching pattern is found later.
vector<std::pair<string, int>> vmodule_values GUARDED_BY(vmodule_mtx);
}  // namespace

void UpdateVmodule() {
  std::lock_guard l(vmodule_mtx);
  // Set everything to 0
  for (auto& module_value : vmodule_values) {
    module_value.second = 0;
  }

  // Set to new requested values
  auto requested_settings = strings::Split(FLAGS_vmodule, ",");
  for (const auto& module_value : requested_settings) {
    if (module_value.empty()) {
      continue;
    }
    vector<string> kv = strings::Split(module_value, "=");

    // Values has been validated in ValidateVmodule
    const int value = static_cast<int>(strtol(kv[1].c_str(), nullptr /* end ptr */, 10));

    auto it = std::find_if(
        vmodule_values.begin(), vmodule_values.end(),
        [&](std::pair<string, int> const& elem) { return elem.first == kv[0]; });

    if (it == vmodule_values.end()) {
      vmodule_values.push_back({kv[0], value});
    } else {
      it->second = value;
    }
  }

  std::vector<string> module_values_str;
  for (auto elem : vmodule_values) {
    module_values_str.emplace_back(Format("$0=$1", elem.first, elem.second));
  }
  std::string set_vmodules = JoinStrings(module_values_str, ",");

  // Directly invoke SetCommandLineOption instead of SetFlagInternal which would result in infinite
  // recursion.
  google::SetCommandLineOption("vmodule", set_vmodules.c_str());

  // Now update previously set modules
  for (auto elem : vmodule_values) {
    google::SetVLOGLevel(elem.first.c_str(), elem.second);
  }
}

namespace flags_internal {
string SetFlagInternal(
    const void* flag_ptr, const char* flag_name, const string& new_value,
    const gflags::FlagSettingMode set_mode) {
  // The gflags library sets new values of flags without synchronization.
  // TODO: patch gflags to use proper synchronization.
  ANNOTATE_IGNORE_WRITES_BEGIN();
  // Try to set the new value.
  string output_msg = google::SetCommandLineOptionWithMode(flag_name, new_value.c_str(), set_mode);
  ANNOTATE_IGNORE_WRITES_END();

  if (output_msg.empty()) {
    return output_msg;
  }

  flags_callback_internal::InvokeCallbacks(flag_ptr, flag_name);

  return output_msg;
}

Status SetFlagInternal(
    const void* flag_ptr, const char* flag_name, const std::string& new_value) {
  auto res = SetFlagInternal(flag_ptr, flag_name, new_value, google::SET_FLAGS_VALUE);
  SCHECK_FORMAT(!res.empty(), InvalidArgument, "Failed to set flag $0: $1", flag_name, new_value);
  return Status::OK();
}

Status SetFlagDefaultAndCurrentInternal(
    const void* flag_ptr, const char* flag_name, const string& value) {
  // SetCommandLineOptionWithMode returns non-empty string on success
  auto res = gflags::SetCommandLineOptionWithMode(
      flag_name, value.c_str(), gflags::FlagSettingMode::SET_FLAGS_DEFAULT);
  SCHECK_FORMAT(
      !res.empty(), InvalidArgument, "Failed to set flag $0 default to value $1", flag_name, value);

  res = SetFlagInternal(flag_ptr, flag_name, value, google::SET_FLAGS_VALUE);
  SCHECK_FORMAT(
      !res.empty(), InvalidArgument, "Failed to set flag $0 to value $1", flag_name, value);
  return Status::OK();
}

Status SetFlag(const std::string* flag_ptr, const char* flag_name, const std::string& new_value) {
  return SetFlagInternal(flag_ptr, flag_name, new_value);
}

Status SetFlagDefaultAndCurrent(
    const std::string* flag_ptr, const char* flag_name, const std::string& new_value) {
  return SetFlagDefaultAndCurrentInternal(flag_ptr, flag_name, new_value);
}

SetFlagResult SetFlag(
    const string& flag_name, const string& new_value, const SetFlagForce force, string* old_value,
    string* output_msg) {
  // Validate that the flag exists and get the current value.
  CommandLineFlagInfo flag_info;
  if (!google::GetCommandLineFlagInfo(flag_name.c_str(), &flag_info)) {
    *output_msg = "Flag does not exist";
    return SetFlagResult::NO_SUCH_FLAG;
  }
  const string& old_val = flag_info.current_value;

  // Validate that the flag is runtime-changeable.
  unordered_set<FlagTag> tags;
  GetFlagTags(flag_name, &tags);
  if (!ContainsKey(tags, FlagTag::kRuntime)) {
    if (force) {
      LOG(WARNING) << "Forcing change of non-runtime-safe flag " << flag_name;
    } else {
      *output_msg = "Flag is not safe to change at runtime";
      return SetFlagResult::NOT_SAFE;
    }
  }

  string ret = flags_internal::SetFlagInternal(
      flag_info.flag_ptr, flag_name.c_str(), new_value, google::SET_FLAGS_VALUE);

  if (ret.empty()) {
    *output_msg = "Unable to set flag: bad value. Check stderr for more information.";
    return SetFlagResult::BAD_VALUE;
  }

  // Callbacks might have changed the value of the flag, so retrieve current value again.
  string final_value;
  // We have already validated the flag_name with GetCommandLineFlagInfo, so this should not fail.
  CHECK(google::GetCommandLineOption(flag_name.c_str(), &final_value));

  bool is_sensitive = ContainsKey(tags, FlagTag::kSensitive_info);
  LOG(INFO) << "Changed flag: " << flag_name << " from '" << (is_sensitive ? "***" : old_val)
            << "' to '" << (is_sensitive ? "***" : final_value) << "'";

  *output_msg = ret;
  *old_value = old_val;

  return SetFlagResult::SUCCESS;
}
}  // namespace flags_internal

} // namespace yb
