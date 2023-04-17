// Copyright (c) YugaByte, Inc.
// This file was auto generated by python/yb/gen_pch.py
#pragma once

#include <assert.h>
#include <dirent.h>
#include <float.h>
#include <inttypes.h>
#include <openssl/ossl_typ.h>
#include <pthread.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <time.h>
#include <uuid/uuid.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <bitset>
#include <cassert>
#include <cfloat>
#include <chrono>
#include <climits>
#include <cmath>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <deque>
#include <functional>
#include <iosfwd>
#include <iterator>
#include <limits>
#include <list>
#include <map>
#include <memory>
#include <mutex>
#include <new>
#include <optional>
#include <ostream>
#include <random>
#include <set>
#include <shared_mutex>
#include <sstream>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include <boost/algorithm/string/predicate.hpp>
#include <boost/asio/ip/address.hpp>
#include <boost/asio/ip/address_v4.hpp>
#include <boost/asio/ip/address_v6.hpp>
#include <boost/atomic.hpp>
#include <boost/container/small_vector.hpp>
#include <boost/core/demangle.hpp>
#include <boost/function.hpp>
#include <boost/functional/hash.hpp>
#include <boost/functional/hash/hash.hpp>
#include <boost/icl/discrete_interval.hpp>
#include <boost/icl/interval_set.hpp>
#include <boost/intrusive/list.hpp>
#include <boost/iterator/transform_iterator.hpp>
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
#include <boost/range/iterator_range.hpp>
#include <boost/signals2/dummy_mutex.hpp>
#include <boost/smart_ptr/detail/yield_k.hpp>
#include <boost/system/error_code.hpp>
#include <boost/tti/has_type.hpp>
#include <boost/type_traits/is_const.hpp>
#include <boost/unordered_map.hpp>
#include <boost/uuid/uuid.hpp>
#include <gflags/gflags.h>
#include <gflags/gflags_declare.h>
#include <glog/logging.h>
#include <google/protobuf/arena.h>
#include <google/protobuf/arenastring.h>
#include <google/protobuf/descriptor.pb.h>
#include <google/protobuf/generated_message_table_driven.h>
#include <google/protobuf/generated_message_util.h>
#include <google/protobuf/io/coded_stream.h>
#include <google/protobuf/map_entry.h>
#include <google/protobuf/map_field_inl.h>
#include <google/protobuf/message.h>
#include <google/protobuf/metadata.h>
#include <google/protobuf/repeated_field.h>
#include <google/protobuf/stubs/common.h>
#include <google/protobuf/unknown_field_set.h>
#include <google/protobuf/wire_format_lite.h>
#include <gtest/gtest.h>
#include <gtest/gtest_prod.h>

#include "yb/gutil/atomicops.h"
#include "yb/gutil/bits.h"
#include "yb/gutil/callback_forward.h"
#include "yb/gutil/casts.h"
#include "yb/gutil/dynamic_annotations.h"
#include "yb/gutil/endian.h"
#include "yb/gutil/hash/builtin_type_hash.h"
#include "yb/gutil/hash/city.h"
#include "yb/gutil/hash/hash.h"
#include "yb/gutil/hash/hash128to64.h"
#include "yb/gutil/hash/jenkins.h"
#include "yb/gutil/hash/jenkins_lookup2.h"
#include "yb/gutil/hash/legacy_hash.h"
#include "yb/gutil/hash/string_hash.h"
#include "yb/gutil/int128.h"
#include "yb/gutil/integral_types.h"
#include "yb/gutil/logging-inl.h"
#include "yb/gutil/macros.h"
#include "yb/gutil/map-util.h"
#include "yb/gutil/mathlimits.h"
#include "yb/gutil/once.h"
#include "yb/gutil/port.h"
#include "yb/gutil/ref_counted.h"
#include "yb/gutil/singleton.h"
#include "yb/gutil/spinlock.h"
#include "yb/gutil/stl_util.h"
#include "yb/gutil/stringprintf.h"
#include "yb/gutil/strings/ascii_ctype.h"
#include "yb/gutil/strings/charset.h"
#include "yb/gutil/strings/escaping.h"
#include "yb/gutil/strings/fastmem.h"
#include "yb/gutil/strings/join.h"
#include "yb/gutil/strings/numbers.h"
#include "yb/gutil/strings/stringpiece.h"
#include "yb/gutil/strings/substitute.h"
#include "yb/gutil/sysinfo.h"
#include "yb/gutil/template_util.h"
#include "yb/gutil/thread_annotations.h"
#include "yb/gutil/type_traits.h"
#include "yb/util/algorithm_util.h"
#include "yb/util/bitmap.h"
#include "yb/util/boost_mutex_utils.h"
#include "yb/util/byte_buffer.h"
#include "yb/util/bytes_formatter.h"
#include "yb/util/cast.h"
#include "yb/util/coding_consts.h"
#include "yb/util/col_group.h"
#include "yb/util/compare_util.h"
#include "yb/util/debug/lock_debug.h"
#include "yb/util/decimal.h"
#include "yb/util/enums.h"
#include "yb/util/env.h"
#include "yb/util/errno.h"
#include "yb/util/fast_varint.h"
#include "yb/util/faststring.h"
#include "yb/util/file_system.h"
#include "yb/util/flags.h"
#include "yb/util/flags/auto_flags.h"
#include "yb/util/flags/flag_tags.h"
#include "yb/util/flags/flags_callback.h"
#include "yb/util/format.h"
#include "yb/util/io.h"
#include "yb/util/kv_util.h"
#include "yb/util/locks.h"
#include "yb/util/math_util.h"
#include "yb/util/memory/arena.h"
#include "yb/util/memory/arena_fwd.h"
#include "yb/util/memory/arena_list.h"
#include "yb/util/memory/mc_types.h"
#include "yb/util/memory/memory.h"
#include "yb/util/monotime.h"
#include "yb/util/mutex.h"
#include "yb/util/net/inetaddress.h"
#include "yb/util/net/net_fwd.h"
#include "yb/util/net/net_util.h"
#include "yb/util/opid.h"
#include "yb/util/physical_time.h"
#include "yb/util/port_picker.h"
#include "yb/util/random.h"
#include "yb/util/random_util.h"
#include "yb/util/ref_cnt_buffer.h"
#include "yb/util/result.h"
#include "yb/util/rw_semaphore.h"
#include "yb/util/size_literals.h"
#include "yb/util/slice.h"
#include "yb/util/status.h"
#include "yb/util/status_ec.h"
#include "yb/util/status_format.h"
#include "yb/util/status_fwd.h"
#include "yb/util/status_log.h"
#include "yb/util/std_util.h"
#include "yb/util/string_case.h"
#include "yb/util/string_trim.h"
#include "yb/util/string_util.h"
#include "yb/util/strongly_typed_bool.h"
#include "yb/util/strongly_typed_string.h"
#include "yb/util/strongly_typed_uuid.h"
#include "yb/util/test_macros.h"
#include "yb/util/test_util.h"
#include "yb/util/timestamp.h"
#include "yb/util/tostring.h"
#include "yb/util/type_traits.h"
#include "yb/util/uint_set.h"
#include "yb/util/ulimit.h"
#include "yb/util/uuid.h"
#include "yb/util/varint.h"
#include "yb/util/yb_partition.h"
