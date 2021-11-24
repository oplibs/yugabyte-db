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

#include "yb/util/debug-util.h"

#include <execinfo.h>
#include <dirent.h>
#include <sys/syscall.h>

#ifdef __linux__
#include <link.h>
#include <cxxabi.h>
#endif // __linux__

#ifdef __linux__
#include <backtrace.h>
#endif // __linux__

#include <string>
#include <iostream>
#include <mutex>
#include <regex>

#include <glog/logging.h>

#include "yb/gutil/hash/city.h"
#include "yb/gutil/linux_syscall_support.h"
#include "yb/gutil/macros.h"
#include "yb/gutil/singleton.h"
#include "yb/gutil/stringprintf.h"
#include "yb/gutil/strings/numbers.h"

#include "yb/util/enums.h"
#include "yb/util/errno.h"
#include "yb/util/format.h"
#include "yb/util/lockfree.h"
#include "yb/util/monotime.h"
#include "yb/util/source_location.h"
#include "yb/util/status.h"
#include "yb/util/thread.h"

using namespace std::literals;

#if defined(__linux__) && !defined(NDEBUG)
constexpr bool kDefaultUseLibbacktrace = true;
#else
constexpr bool kDefaultUseLibbacktrace = false;
#endif

DEFINE_bool(use_libbacktrace, kDefaultUseLibbacktrace,
            "Whether to use the libbacktrace library for symbolizing stack traces");

// Evil hack to grab a few useful functions from glog
namespace google {

extern int GetStackTrace(void** result, int max_depth, int skip_count);

// Symbolizes a program counter.  On success, returns true and write the
// symbol name to "out".  The symbol name is demangled if possible
// (supports symbols generated by GCC 3.x or newer).  Otherwise,
// returns false.
bool Symbolize(void *pc, char *out, int out_size);

namespace glog_internal_namespace_ {
extern void DumpStackTraceToString(std::string *s);
} // namespace glog_internal_namespace_
} // namespace google

// The %p field width for printf() functions is two characters per byte.
// For some environments, add two extra bytes for the leading "0x".
static const int kPrintfPointerFieldWidth = 2 + 2 * sizeof(void*);

using std::string;

namespace yb {

// https://gcc.gnu.org/onlinedocs/libstdc++/libstdc++-html-USERS-4.3/a01696.html
enum DemangleStatus : int {
  kDemangleOk = 0,
  kDemangleMemAllocFailure = -1,
  kDemangleInvalidMangledName = -2,
  kDemangleInvalidArgument = -3
};


namespace {

const char kUnknownSymbol[] = "(unknown)";
const char kStackTraceEntryFormat[] = "    @ %*p  %s";

#ifdef __linux__

// Remove path prefixes up to what looks like the root of the YB source tree, or the source tree
// of other recognizable codebases.
const char* NormalizeSourceFilePath(const char* file_path) {
  if (file_path == nullptr) {
    return file_path;
  }

  // Remove the leading "../../../" stuff.
  while (strncmp(file_path, "../", 3) == 0) {
    file_path += 3;
  }

  // This could be called arbitrarily early or late in program execution as part of backtrace,
  // so we're not using any std::string static constants here.
#define YB_HANDLE_SOURCE_SUBPATH(subpath, prefix_len_to_remove) \
    do { \
      const char* const subpath_ptr = strstr(file_path, (subpath)); \
      if (subpath_ptr != nullptr) { \
        return subpath_ptr + (prefix_len_to_remove); \
      } \
    } while (0);

  YB_HANDLE_SOURCE_SUBPATH("/src/yb/", 5);
  YB_HANDLE_SOURCE_SUBPATH("/src/postgres/src/", 5);
  YB_HANDLE_SOURCE_SUBPATH("/src/rocksdb/", 5);
  YB_HANDLE_SOURCE_SUBPATH("/thirdparty/build/", 1);

  // These are Linuxbrew gcc's standard headers. Keep the path starting from "gcc/...".
  YB_HANDLE_SOURCE_SUBPATH("/Cellar/gcc/", 8);

  // TODO: replace postgres_build with just postgres.
  YB_HANDLE_SOURCE_SUBPATH("/postgres_build/src/", 1);

#undef YB_HANDLE_SOURCE_SUBPATH

  return file_path;
}

struct SymbolizationContext {
  StackTraceLineFormat stack_trace_line_format = StackTraceLineFormat::DEFAULT;
  string* buf = nullptr;
};

void BacktraceErrorCallback(void* data, const char* msg, int errnum) {
  bool reported = false;
  string* buf_ptr = nullptr;
  if (data) {
    auto* context = static_cast<SymbolizationContext*>(data);
    if (context->buf) {
      buf_ptr = context->buf;
      buf_ptr->append(StringPrintf("Backtrace error: %s (errnum=%d)\n", msg, errnum));
      reported = true;
    }
  }

  if (!reported) {
    // A backup mechanism for error reporting.
    fprintf(stderr, "%s called with data=%p, msg=%s, errnum=%d, buf_ptr=%p\n", __func__,
            data, msg, errnum, buf_ptr);
  }
}

class GlobalBacktraceState {
 public:
  GlobalBacktraceState() {
    bt_state_ = backtrace_create_state(
        /* filename */ nullptr,
        /* threaded = */ 0,
        BacktraceErrorCallback,
        /* data */ nullptr);

    // To complete initialization we should call backtrace, otherwise it could fail in case of
    // concurrent initialization.
    // We do this in a separate thread to avoid deadlock.
    // TODO: implement proper fix to avoid other potential deadlocks/stuck threads related
    // to locking common mutexes from signal handler. See
    // https://github.com/yugabyte/yugabyte-db/issues/6672 for more details.
    std::thread backtrace_thread([&] {
      backtrace_full(bt_state_, /* skip = */ 1, DummyCallback,
                     BacktraceErrorCallback, nullptr);
    });
    backtrace_thread.join();
  }

  backtrace_state* GetState() { return bt_state_; }

  std::mutex* mutex() { return &mutex_; }

 private:
  static int DummyCallback(void *const data, const uintptr_t pc,
                    const char* const filename, const int lineno,
                    const char* const original_function_name) {
    return 0;
  }

  struct backtrace_state* bt_state_;
  std::mutex mutex_;
};

int BacktraceFullCallback(void *const data, const uintptr_t pc,
                          const char* const filename, const int lineno,
                          const char* const original_function_name) {
  assert(data != nullptr);
  const SymbolizationContext& context = *pointer_cast<SymbolizationContext*>(data);
  string* const buf = context.buf;
  int demangle_status = 0;
  char* const demangled_function_name =
      original_function_name != nullptr ?
      abi::__cxa_demangle(original_function_name,
                          nullptr,  // output_buffer
                          nullptr,  // length
                          &demangle_status) :
      nullptr;
  const char* function_name_to_use = original_function_name;
  if (original_function_name != nullptr) {
    if (demangle_status != kDemangleOk) {
      if (demangle_status != kDemangleInvalidMangledName) {
        // -2 means the mangled name is not a valid name under the C++ ABI mangling rules.
        // This happens when the name is e.g. "main", so we don't report the error.
        StringAppendF(buf, "Error: __cxa_demangle failed for '%s' with error code %d\n",
            original_function_name, demangle_status);
      }
      // Regardless of the exact reason for demangle failure, we use the original function name
      // provided by libbacktrace.
    } else if (demangled_function_name != nullptr) {
      // If __cxa_demangle returns 0 and a non-null string, we use that instead of the original
      // function name.
      function_name_to_use = demangled_function_name;
    } else {
      StringAppendF(buf,
          "Error: __cxa_demangle returned zero status but nullptr demangled function for '%s'\n",
          original_function_name);
    }
  }

  string pretty_function_name;
  if (function_name_to_use == nullptr) {
    pretty_function_name = kUnknownSymbol;
  } else {
    // Allocating regexes on the heap so that they would never get deallocated. This is because
    // the kernel watchdog thread could still be symbolizing stack traces as global destructors
    // are being called.
    static const std::regex* kStdColonColonOneRE = new std::regex("\\bstd::__1::\\b");
    pretty_function_name = std::regex_replace(function_name_to_use, *kStdColonColonOneRE, "std::");

    static const std::regex* kStringRE = new std::regex(
        "\\bstd::basic_string<char, std::char_traits<char>, std::allocator<char> >");
    pretty_function_name = std::regex_replace(pretty_function_name, *kStringRE, "string");

    static const std::regex* kRemoveStdPrefixRE =
        new std::regex("\\bstd::(string|tuple|shared_ptr|unique_ptr)\\b");
    pretty_function_name = std::regex_replace(pretty_function_name, *kRemoveStdPrefixRE, "$1");
  }

  const bool is_symbol_only_fmt =
      context.stack_trace_line_format == StackTraceLineFormat::SYMBOL_ONLY;

  // We have not appended an end-of-line character yet. Let's see if we have file name / line number
  // information first. BTW kStackTraceEntryFormat is used both here and in glog-based
  // symbolization.
  if (filename == nullptr) {
    // libbacktrace failed to produce an address. We still need to produce a line of the form:
    // "    @     0x7f2d98f9bd87  "
    char hex_pc_buf[32];
    snprintf(hex_pc_buf, sizeof(hex_pc_buf), "0x%" PRIxPTR, pc);
    if (is_symbol_only_fmt) {
      // At least print out the hex address, otherwise we'd end up with an empty string.
      *buf += hex_pc_buf;
    } else {
      StringAppendF(buf, "    @ %18s ", hex_pc_buf);
    }
  } else {
    const string frame_without_file_line =
        is_symbol_only_fmt ? pretty_function_name
                           : StringPrintf(
                               kStackTraceEntryFormat, kPrintfPointerFieldWidth,
                               reinterpret_cast<void*>(pc), pretty_function_name.c_str());

    // Got filename and line number from libbacktrace! No need to filter the output through
    // addr2line, etc.
    switch (context.stack_trace_line_format) {
      case StackTraceLineFormat::CLION_CLICKABLE: {
        const string file_line_prefix = StringPrintf("%s:%d: ", filename, lineno);
        StringAppendF(buf, "%-100s", file_line_prefix.c_str());
        *buf += frame_without_file_line;
        break;
      }
      case StackTraceLineFormat::SHORT: {
        *buf += frame_without_file_line;
        StringAppendF(buf, " (%s:%d)", NormalizeSourceFilePath(filename), lineno);
        break;
      }
      case StackTraceLineFormat::SYMBOL_ONLY: {
        *buf += frame_without_file_line;
        StringAppendF(buf, " (%s:%d)", NormalizeSourceFilePath(filename), lineno);
        break;
      }
    }
  }

  buf->push_back('\n');

  // No need to check for nullptr, free is a no-op in that case.
  free(demangled_function_name);
  return 0;
}
#endif  // __linux__

bool IsDoubleUnderscoredAndInList(
    const char* symbol, const std::initializer_list<const char*>& list) {
  if (symbol[0] != '_' || symbol[1] != '_') {
    return false;
  }
  for (const auto* idle_function : list) {
    if (!strcmp(symbol, idle_function)) {
      return true;
    }
  }
  return false;
}

bool IsIdle(const char* symbol) {
  return IsDoubleUnderscoredAndInList(symbol,
                                      { "__GI_epoll_wait",
                                        "__pthread_cond_timedwait",
                                        "__pthread_cond_wait" });
}

bool IsWaiting(const char* symbol) {
  return IsDoubleUnderscoredAndInList(symbol,
                                      { "__GI___pthread_mutex_lock" });
}

}  // anonymous namespace

Status ListThreads(std::vector<pid_t> *tids) {
#if defined(__linux__)
  DIR *dir = opendir("/proc/self/task/");
  if (dir == NULL) {
    return STATUS(IOError, "failed to open task dir", Errno(errno));
  }
  struct dirent *d;
  while ((d = readdir(dir)) != NULL) {
    if (d->d_name[0] != '.') {
      uint32_t tid;
      if (!safe_strtou32(d->d_name, &tid)) {
        LOG(WARNING) << "bad tid found in procfs: " << d->d_name;
        continue;
      }
      tids->push_back(tid);
    }
  }
  closedir(dir);
#endif // defined(__linux__)
  return Status::OK();
}

std::string GetStackTrace(StackTraceLineFormat stack_trace_line_format,
                          int num_top_frames_to_skip) {
  std::string buf;
#ifdef __linux__
  if (FLAGS_use_libbacktrace) {
    SymbolizationContext context;
    context.buf = &buf;
    context.stack_trace_line_format = stack_trace_line_format;

    // Use libbacktrace on Linux because that gives us file names and line numbers.
    auto* global_backtrace_state = Singleton<GlobalBacktraceState>::get();
    struct backtrace_state* const backtrace_state = global_backtrace_state->GetState();

    // Avoid multi-threaded access to libbacktrace which causes high memory consumption.
    std::lock_guard<std::mutex> l(*global_backtrace_state->mutex());

    // TODO: https://yugabyte.atlassian.net/browse/ENG-4729

    const int backtrace_full_rv = backtrace_full(
        backtrace_state, /* skip = */ num_top_frames_to_skip + 1, BacktraceFullCallback,
        BacktraceErrorCallback, &context);
    if (backtrace_full_rv != 0) {
      StringAppendF(&buf, "Error: backtrace_full return value is %d", backtrace_full_rv);
    }
    return buf;
  }
#endif
  google::glog_internal_namespace_::DumpStackTraceToString(&buf);
  return buf;
}

std::string GetStackTraceHex() {
  char buf[1024];
  HexStackTraceToString(buf, 1024);
  return std::string(buf);
}

void HexStackTraceToString(char* buf, size_t size) {
  StackTrace trace;
  trace.Collect(1);
  trace.StringifyToHex(buf, size);
}

string GetLogFormatStackTraceHex() {
  StackTrace trace;
  trace.Collect(1);
  return trace.ToLogFormatHexString();
}

void StackTrace::Collect(int skip_frames) {
#if THREAD_SANITIZER || ADDRESS_SANITIZER
  num_frames_ = google::GetStackTrace(frames_, arraysize(frames_), skip_frames);
#else
  int max_frames = skip_frames + arraysize(frames_);
  void** buffer = static_cast<void**>(alloca((max_frames) * sizeof(void*)));
  num_frames_ = backtrace(buffer, max_frames);
  if (num_frames_ > skip_frames) {
    num_frames_ -= skip_frames;
    memmove(frames_, buffer + skip_frames, num_frames_ * sizeof(void*));
  } else {
    num_frames_ = 0;
  }
#endif
}

void StackTrace::StringifyToHex(char* buf, size_t size, int flags) const {
  char* dst = buf;

  // Reserve kHexEntryLength for the first iteration of the loop, 1 byte for a
  // space (which we may not need if there's just one frame), and 1 for a nul
  // terminator.
  char* limit = dst + size - kHexEntryLength - 2;
  for (int i = 0; i < num_frames_ && dst < limit; i++) {
    if (i != 0) {
      *dst++ = ' ';
    }
    // See note in Symbolize() below about why we subtract 1 from each address here.
    uintptr_t addr = reinterpret_cast<uintptr_t>(frames_[i]);
    if (!(flags & NO_FIX_CALLER_ADDRESSES)) {
      addr--;
    }
    FastHex64ToBuffer(addr, dst);
    dst += kHexEntryLength;
  }
  *dst = '\0';
}

string StackTrace::ToHexString(int flags) const {
  // Each frame requires kHexEntryLength, plus a space
  // We also need one more byte at the end for '\0'
  char buf[kMaxFrames * (kHexEntryLength + 1) + 1];
  StringifyToHex(buf, arraysize(buf), flags);
  return string(buf);
}

// If group is specified it is filled with value corresponding to this stack trace.
void SymbolizeAddress(
    const StackTraceLineFormat stack_trace_line_format,
    void* pc,
    string* buf,
    StackTraceGroup* group = nullptr
#ifdef __linux__
    , GlobalBacktraceState* global_backtrace_state = nullptr
#endif
    ) {
  // The return address 'pc' on the stack is the address of the instruction
  // following the 'call' instruction. In the case of calling a function annotated
  // 'noreturn', this address may actually be the first instruction of the next
  // function, because the function we care about ends with the 'call'.
  // So, we subtract 1 from 'pc' so that we're pointing at the 'call' instead
  // of the return address.
  //
  // For example, compiling a C program with -O2 that simply calls 'abort()' yields
  // the following disassembly:
  //     Disassembly of section .text:
  //
  //     0000000000400440 <main>:
  //       400440:   48 83 ec 08             sub    $0x8,%rsp
  //       400444:   e8 c7 ff ff ff          callq  400410 <abort@plt>
  //
  //     0000000000400449 <_start>:
  //       400449:   31 ed                   xor    %ebp,%ebp
  //       ...
  //
  // If we were to take a stack trace while inside 'abort', the return pointer
  // on the stack would be 0x400449 (the first instruction of '_start'). By subtracting
  // 1, we end up with 0x400448, which is still within 'main'.
  //
  // This also ensures that we point at the correct line number when using addr2line
  // on logged stacks.
  pc = reinterpret_cast<void*>(reinterpret_cast<size_t>(pc) - 1);
#ifdef __linux__
  if (FLAGS_use_libbacktrace) {
    if (!global_backtrace_state) {
      global_backtrace_state = Singleton<GlobalBacktraceState>::get();
    }
    struct backtrace_state* const backtrace_state = global_backtrace_state->GetState();

    // Avoid multi-threaded access to libbacktrace which causes high memory consumption.
    std::lock_guard<std::mutex> l(*global_backtrace_state->mutex());

    SymbolizationContext context;
    context.stack_trace_line_format = stack_trace_line_format;
    context.buf = buf;
    backtrace_pcinfo(backtrace_state, reinterpret_cast<uintptr_t>(pc),
                    BacktraceFullCallback, BacktraceErrorCallback, &context);
    return;
  }
#endif
  char tmp[1024];
  const char* symbol = kUnknownSymbol;

  if (google::Symbolize(pc, tmp, sizeof(tmp))) {
    symbol = tmp;
    if (group) {
      if (IsWaiting(symbol)) {
        *group = StackTraceGroup::kWaiting;
      } else if (IsIdle(symbol)) {
        *group = StackTraceGroup::kIdle;
      }
    }
  }

  StringAppendF(buf, kStackTraceEntryFormat, kPrintfPointerFieldWidth, pc, symbol);
  // We are appending the end-of-line character separately because we want to reuse the same
  // format string for libbacktrace callback and glog-based symbolization, and we have an extra
  // file name / line number component before the end-of-line in the libbacktrace case.
  buf->push_back('\n');
}

// Symbolization function borrowed from glog and modified to use libbacktrace on Linux.
string StackTrace::Symbolize(
    const StackTraceLineFormat stack_trace_line_format, StackTraceGroup* group) const {
  string buf;
#ifdef __linux__
  // Use libbacktrace for symbolization.
  auto* global_backtrace_state =
      FLAGS_use_libbacktrace ? Singleton<GlobalBacktraceState>::get() : nullptr;
#endif

  if (group) {
    *group = StackTraceGroup::kActive;
  }

  for (int i = 0; i < num_frames_; i++) {
    void* const pc = frames_[i];

    SymbolizeAddress(stack_trace_line_format, pc, &buf, group
#ifdef __linux__
        , global_backtrace_state
#endif
        );
  }

  return buf;
}

string StackTrace::ToLogFormatHexString() const {
  string buf;
  for (int i = 0; i < num_frames_; i++) {
    void* pc = frames_[i];
    StringAppendF(&buf, "    @ %*p\n", kPrintfPointerFieldWidth, pc);
  }
  return buf;
}

uint64_t StackTrace::HashCode() const {
  return util_hash::CityHash64(reinterpret_cast<const char*>(frames_),
                               sizeof(frames_[0]) * num_frames_);
}

namespace {
#ifdef __linux__
int DynamcLibraryListCallback(struct dl_phdr_info *info, size_t size, void *data) {
  if (*info->dlpi_name != '\0') {
    // We can't use LOG(...) yet because Google Logging might not be initialized.
    // It is also important to write the entire line at once so that it is less likely to be
    // interleaved with pieces of similar lines from other processes.
    std::cerr << StringPrintf(
        "Shared library '%s' loaded at address 0x%" PRIx64 "\n", info->dlpi_name, info->dlpi_addr);
  }
  return 0;
}
#endif

void PrintLoadedDynamicLibraries() {
#ifdef __linux__
  // Supported on Linux only.
  dl_iterate_phdr(DynamcLibraryListCallback, nullptr);
#endif
}

bool PrintLoadedDynamicLibrariesOnceHelper() {
  const char* list_dl_env_var = std::getenv("YB_LIST_LOADED_DYNAMIC_LIBS");
  if (list_dl_env_var != nullptr && *list_dl_env_var != '\0') {
    PrintLoadedDynamicLibraries();
  }
  return true;
}

} // anonymous namespace

string SymbolizeAddress(void *pc, const StackTraceLineFormat stack_trace_line_format) {
  string s;
  SymbolizeAddress(stack_trace_line_format, pc, &s);
  return s;
}

// ------------------------------------------------------------------------------------------------
// Tracing function calls
// ------------------------------------------------------------------------------------------------

namespace {

// List the load addresses of dynamic libraries once on process startup if required.
const bool  __attribute__((unused)) kPrintedLoadedDynamicLibraries =
    PrintLoadedDynamicLibrariesOnceHelper();

}  // namespace

std::string DemangleName(const char* mangled_name) {
  int demangle_status = 0;
  char* demangled_name =
      abi::__cxa_demangle(mangled_name, nullptr /* output_buffer */, nullptr /* length */,
                          &demangle_status);
  string ret_val = demangle_status == kDemangleOk ? demangled_name : mangled_name;
  free(demangled_name);
  return ret_val;
}

std::string SourceLocation::ToString() const {
  return Format("$0:$1", file_name, line_number);
}

ScopeLogger::ScopeLogger(const std::string& msg, std::function<void()> on_scope_bounds)
    : msg_(msg), on_scope_bounds_(std::move(on_scope_bounds)) {
  LOG(INFO) << ">>> " << msg_;
  on_scope_bounds_();
}

ScopeLogger::~ScopeLogger() {
  on_scope_bounds_();
  LOG(INFO) << "<<< " << msg_;
}

}  // namespace yb
