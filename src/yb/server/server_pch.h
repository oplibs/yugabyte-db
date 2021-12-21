// Copyright (c) YugaByte, Inc.
// This file was auto generated by python/yb/gen_pch.py
#pragma once

#include <assert.h>
#include <dirent.h>
#include <float.h>
#include <inttypes.h>
#include <math.h>
#include <openssl/ossl_typ.h>
#include <pthread.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <sys/uio.h>
#include <time.h>
#include <unistd.h>
#include <uuid/uuid.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <bitset>
#include <cassert>
#include <cfloat>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdarg>
#include <cstdint>
#include <cstdlib>
#include <ctime>
#include <deque>
#include <functional>
#include <future>
#include <iosfwd>
#include <iterator>
#include <limits>
#include <list>
#include <map>
#include <memory>
#include <mutex>
#include <random>
#include <set>
#include <shared_mutex>
#include <sstream>
#include <stack>
#include <string>
#include <thread>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include <boost/algorithm/string.hpp>
#include <boost/algorithm/string/predicate.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/atomic.hpp>
#include <boost/circular_buffer.hpp>
#include <boost/container/small_vector.hpp>
#include <boost/core/demangle.hpp>
#include <boost/core/enable_if.hpp>
#include <boost/function.hpp>
#include <boost/functional/hash.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/lockfree/queue.hpp>
#include <boost/mpl/and.hpp>
#include <boost/mpl/if.hpp>
#include <boost/optional.hpp>
#include <boost/optional/optional.hpp>
#include <boost/optional/optional_fwd.hpp>
#include <boost/preprocessor/cat.hpp>
#include <boost/preprocessor/config/config.hpp>
#include <boost/preprocessor/expr_if.hpp>
#include <boost/preprocessor/facilities/apply.hpp>
#include <boost/preprocessor/if.hpp>
#include <boost/preprocessor/punctuation/is_begin_parens.hpp>
#include <boost/preprocessor/seq/enum.hpp>
#include <boost/preprocessor/seq/for_each.hpp>
#include <boost/preprocessor/seq/transform.hpp>
#include <boost/preprocessor/stringize.hpp>
#include <boost/preprocessor/variadic/to_seq.hpp>
#include <boost/signals2/dummy_mutex.hpp>
#include <boost/smart_ptr/detail/yield_k.hpp>
#include <boost/tti/has_type.hpp>
#include <boost/type_traits.hpp>
#include <boost/type_traits/is_const.hpp>
#include <boost/type_traits/make_signed.hpp>
#include <boost/uuid/uuid.hpp>
#include <boost/version.hpp>
#include <cds/init.h>
#undef EV_ERROR // On mac is it defined as some number, but ev++.h uses it in enum
#include <ev++.h>
#include <gflags/gflags.h>
#include <gflags/gflags_declare.h>
#include <glog/logging.h>
#include <google/protobuf/io/coded_stream.h>
#include <google/protobuf/message.h>
#include <google/protobuf/wire_format_lite.h>
#include <gtest/gtest.h>
#include <gtest/gtest_prod.h>
#include <rapidjson/document.h>
#include <squeasel.h>

#include "yb/gutil/atomicops.h"
#include "yb/gutil/bind.h"
#include "yb/gutil/bind_internal.h"
#include "yb/gutil/callback.h"
#include "yb/gutil/callback_forward.h"
#include "yb/gutil/callback_internal.h"
#include "yb/gutil/casts.h"
#include "yb/gutil/dynamic_annotations.h"
#include "yb/gutil/endian.h"
#include "yb/gutil/int128.h"
#include "yb/gutil/integral_types.h"
#include "yb/gutil/logging-inl.h"
#include "yb/gutil/macros.h"
#include "yb/gutil/map-util.h"
#include "yb/gutil/mathlimits.h"
#include "yb/gutil/once.h"
#include "yb/gutil/port.h"
#include "yb/gutil/ref_counted.h"
#include "yb/gutil/ref_counted_memory.h"
#include "yb/gutil/singleton.h"
#include "yb/gutil/spinlock.h"
#include "yb/gutil/stl_util.h"
#include "yb/gutil/stringprintf.h"
#include "yb/gutil/strings/ascii_ctype.h"
#include "yb/gutil/strings/charset.h"
#include "yb/gutil/strings/escaping.h"
#include "yb/gutil/strings/fastmem.h"
#include "yb/gutil/strings/human_readable.h"
#include "yb/gutil/strings/join.h"
#include "yb/gutil/strings/numbers.h"
#include "yb/gutil/strings/split.h"
#include "yb/gutil/strings/split_internal.h"
#include "yb/gutil/strings/strcat.h"
#include "yb/gutil/strings/stringpiece.h"
#include "yb/gutil/strings/strip.h"
#include "yb/gutil/strings/substitute.h"
#include "yb/gutil/strings/util.h"
#include "yb/gutil/sysinfo.h"
#include "yb/gutil/template_util.h"
#include "yb/gutil/thread_annotations.h"
#include "yb/gutil/threading/thread_collision_warner.h"
#include "yb/gutil/type_traits.h"
#include "yb/gutil/walltime.h"
#include "yb/util/async_util.h"
#include "yb/util/atomic.h"
#include "yb/util/blocking_queue.h"
#include "yb/util/boost_mutex_utils.h"
#include "yb/util/bytes_formatter.h"
#include "yb/util/cast.h"
#include "yb/util/coding_consts.h"
#include "yb/util/compare_util.h"
#include "yb/util/concurrent_pod.h"
#include "yb/util/concurrent_value.h"
#include "yb/util/condition_variable.h"
#include "yb/util/countdown_latch.h"
#include "yb/util/cross_thread_mutex.h"
#include "yb/util/curl_util.h"
#include "yb/util/debug/long_operation_tracker.h"
#include "yb/util/debug/trace_event_impl.h"
#include "yb/util/enums.h"
#include "yb/util/env.h"
#include "yb/util/env_util.h"
#include "yb/util/errno.h"
#include "yb/util/faststring.h"
#include "yb/util/file_system.h"
#include "yb/util/flag_tags.h"
#include "yb/util/flags.h"
#include "yb/util/format.h"
#include "yb/util/jsonwriter.h"
#include "yb/util/locks.h"
#include "yb/util/logging.h"
#include "yb/util/logging_callback.h"
#include "yb/util/math_util.h"
#include "yb/util/mem_tracker.h"
#include "yb/util/memory/arena.h"
#include "yb/util/memory/arena_fwd.h"
#include "yb/util/memory/memory.h"
#include "yb/util/memory/memory_usage.h"
#include "yb/util/metric_entity.h"
#include "yb/util/metrics.h"
#include "yb/util/metrics_fwd.h"
#include "yb/util/metrics_writer.h"
#include "yb/util/monotime.h"
#include "yb/util/mutex.h"
#include "yb/util/net/net_fwd.h"
#include "yb/util/net/net_util.h"
#include "yb/util/net/sockaddr.h"
#include "yb/util/net/socket.h"
#include "yb/util/ntp_clock.h"
#include "yb/util/object_pool.h"
#include "yb/util/operation_counter.h"
#include "yb/util/path_util.h"
#include "yb/util/pb_util.h"
#include "yb/util/physical_time.h"
#include "yb/util/port_picker.h"
#include "yb/util/random.h"
#include "yb/util/random_util.h"
#include "yb/util/ref_cnt_buffer.h"
#include "yb/util/result.h"
#include "yb/util/rolling_log.h"
#include "yb/util/rw_semaphore.h"
#include "yb/util/scope_exit.h"
#include "yb/util/shared_lock.h"
#include "yb/util/signal_util.h"
#include "yb/util/slice.h"
#include "yb/util/spinlock_profiling.h"
#include "yb/util/stack_trace.h"
#include "yb/util/status.h"
#include "yb/util/status_callback.h"
#include "yb/util/status_ec.h"
#include "yb/util/status_format.h"
#include "yb/util/status_fwd.h"
#include "yb/util/status_log.h"
#include "yb/util/stol_utils.h"
#include "yb/util/string_trim.h"
#include "yb/util/striped64.h"
#include "yb/util/strongly_typed_bool.h"
#include "yb/util/strongly_typed_string.h"
#include "yb/util/test_macros.h"
#include "yb/util/test_util.h"
#include "yb/util/thread.h"
#include "yb/util/threadlocal.h"
#include "yb/util/tostring.h"
#include "yb/util/trace.h"
#include "yb/util/type_traits.h"
#include "yb/util/ulimit.h"
#include "yb/util/url-coding.h"
#include "yb/util/uuid.h"
#include "yb/util/version_info.h"
#include "yb/util/version_info.pb.h"
#include "yb/util/web_callback_registry.h"
#include "yb/util/zlib.h"
