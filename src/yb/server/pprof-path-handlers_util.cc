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

#if YB_GOOGLE_TCMALLOC

#include "yb/server/pprof-path-handlers_util.h"

#include <tcmalloc/malloc_extension.h>

#include <cstdint>
#include <iomanip>
#include <string>
#include <utility>

#include <glog/logging.h>

#include "yb/util/format.h"
#include "yb/util/monotime.h"


DECLARE_bool(enable_process_lifetime_heap_profiling);

// Abseil already implements symbolization. Just import their hidden symbol.
namespace absl {

// Symbolizes a program counter (instruction pointer value) `pc` and, on
// success, writes the name to `out`. The symbol name is demangled, if possible.
// Note that the symbolized name may be truncated and will be NUL-terminated.
// Demangling is supported for symbols generated by GCC 3.x or newer). Returns
// `false` on failure.
bool Symbolize(const void *pc, char *out, int out_size);

}


namespace yb {


tcmalloc::Profile GetAllocationProfile(int seconds, int64_t sample_freq_bytes) {
  auto prev_sample_rate = tcmalloc::MallocExtension::GetProfileSamplingRate();
  tcmalloc::MallocExtension::SetProfileSamplingRate(sample_freq_bytes);
  tcmalloc::MallocExtension::AllocationProfilingToken token;
  token = tcmalloc::MallocExtension::StartLifetimeProfiling();

  LOG(INFO) << Format("Sleeping for $0 seconds while profile is collected.", seconds);
  SleepFor(MonoDelta::FromSeconds(seconds));
  tcmalloc::MallocExtension::SetProfileSamplingRate(prev_sample_rate);
  return std::move(token).Stop();
}

tcmalloc::Profile GetHeapSnapshot(HeapSnapshotType snapshot_type) {
  if (snapshot_type == PEAK_HEAP) {
    return tcmalloc::MallocExtension::SnapshotCurrent(tcmalloc::ProfileType::kPeakHeap);
  } else {
    return tcmalloc::MallocExtension::SnapshotCurrent(tcmalloc::ProfileType::kHeap);
  }
}

std::vector<Sample> AggregateAndSortProfile(const tcmalloc::Profile& profile, bool only_growth) {
  LOG(INFO) << "Analyzing TCMalloc sampling profile";
  int failed_symbolizations = 0;
  std::unordered_map<std::string, SampleInfo> samples_map;

  profile.Iterate([&](const tcmalloc::Profile::Sample& sample) {
    // Deallocation samples are the same as the allocation samples, except with a negative
    // sample.count < 0 and the deallocation stack. Skip since we are not currently interested in
    // printing the deallocation stack.
    if (sample.count <= 0) {
      return;
    }

    // If we only want growth, exclude samples for which we saw a deallocation event.
    // "Censored" means we observed an allocation but not a deallocation. (Deallocation-only events
    // are not reported).
    if (only_growth && !sample.is_censored) {
      return;
    }

    std::stringstream sstream;
    char buf[256];
    for (int64_t i = 0; i < sample.depth; ++i) {
      if (absl::Symbolize(sample.stack[i], buf, sizeof(buf))) {
        sstream << buf << std::endl;
      } else {
        ++failed_symbolizations;
        sstream << "Failed to symbolize" << std::endl;
      }
    }
    std::string stack = sstream.str();

    auto& entry = samples_map[stack];
    entry.bytes += sample.allocated_size;
    ++entry.count;

    VLOG(1) << "Sampled stack: " << stack
            << ", sum: " << sample.sum
            << ", count: " << sample.count
            << ", requested_size: " << sample.requested_size
            << ", allocated_size: " << sample.allocated_size
            << ", is_censored: " << sample.is_censored
            << ", avg_lifetime: " << sample.avg_lifetime
            << ", allocator_deallocator_cpu_matched: "
            << sample.allocator_deallocator_cpu_matched.value_or("N/A");
  });
  if (failed_symbolizations > 0) {
    LOG(WARNING) << Format("Failed to symbolize $0 symbols", failed_symbolizations);
  }

  std::vector<Sample> samples_vec;
  samples_vec.reserve(samples_map.size());
  for (auto& entry : samples_map) {
    samples_vec.push_back(std::move(entry));
  }
  std::sort(samples_vec.begin(), samples_vec.end(),
      [](const Sample& a, const Sample& b) {return a.second.bytes > b.second.bytes; });
  return samples_vec;
}

void GenerateTable(std::stringstream* output, const std::vector<Sample>& samples,
    const std::string& title, size_t max_call_stacks) {
  // Generate the output table.
  (*output) << std::fixed;
  (*output) << std::setprecision(2);
  (*output) << Format("<b>Top $0 Call Stacks for: $1</b>\n", max_call_stacks, title);
  if (samples.size() > max_call_stacks) {
    (*output) << Format("$0 call stacks truncated\n", samples.size() - max_call_stacks);
  }
  (*output) << "<p>\n";
  (*output) << "<table style=\"border-collapse: collapse\" border=1 cellpadding=5>\n";
  (*output) << "<tr>\n";
  (*output) << "<th>Total bytes</th>\n";
  (*output) << "<th>Count</th>\n";
  (*output) << "<th>Avg bytes</th>\n";
  (*output) << "<th>Call Stack</th>\n";
  (*output) << "</tr>\n";

  for (size_t i = 0; i < std::min(max_call_stacks, samples.size()); ++i) {
    const auto& entry = samples.at(i);
    (*output) << "<tr>";
    (*output) << Format("<td>$0</td>", entry.second.bytes);
    (*output) << Format("<td>$0</td>", entry.second.count);
    (*output) << Format("<td>$0</td>",
        entry.second.count <= 0 ? 0 : entry.second.bytes / entry.second.count);
    (*output) << Format("<td><pre>$0</pre></td>", entry.first);
    (*output) << "</tr>";
  }
  (*output) << "</table>";
}

} // namespace yb

#endif // YB_GOOGLE_TCMALLOC
