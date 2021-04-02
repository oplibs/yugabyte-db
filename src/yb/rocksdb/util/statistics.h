//  Copyright (c) 2011-present, Facebook, Inc.  All rights reserved.
//  This source code is licensed under the BSD-style license found in the
//  LICENSE file in the root directory of this source tree. An additional grant
//  of patent rights can be found in the PATENTS file in the same directory.
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
#ifndef YB_ROCKSDB_UTIL_STATISTICS_H
#define YB_ROCKSDB_UTIL_STATISTICS_H

#include "yb/rocksdb/statistics.h"

#include <vector>
#include <atomic>
#include <string>

#include "yb/gutil/ref_counted.h"
#include "yb/rocksdb/util/histogram.h"
#include "yb/rocksdb/util/mutexlock.h"
#include "yb/rocksdb/port/likely.h"

namespace yb {
class MetricEntity;
class Histogram;
class HistogramPrototype;

template <class T>
class AtomicGauge;
template <class T>
class GaugePrototype;
}  // namespace yb

namespace rocksdb {

class StatisticsMetricPrototypes;

class StatisticsMetricImpl : public Statistics {
 public:
  StatisticsMetricImpl(
      const scoped_refptr<yb::MetricEntity>& hist_entity,
      const scoped_refptr<yb::MetricEntity>& tick_entity,
      const bool for_intents);

  virtual ~StatisticsMetricImpl();

  uint64_t getTickerCount(uint32_t ticker_type) const override;
  void histogramData(uint32_t histogram_type, HistogramData* const data) const override;

  void setTickerCount(uint32_t ticker_type, uint64_t count) override;
  void recordTick(uint32_t ticker_type, uint64_t count) override;
  void measureTime(uint32_t histogram_type, uint64_t value) override;
  void resetTickersForTest() override;

 private:
  std::vector<scoped_refptr<yb::Histogram>> histograms_;
  std::vector<scoped_refptr<yb::AtomicGauge<uint64_t>>> tickers_;
};

// Utility functions
inline void MeasureTime(Statistics* statistics, uint32_t histogram_type,
                        uint64_t value) {
  if (statistics) {
    statistics->measureTime(histogram_type, value);
  }
}

inline void RecordTick(Statistics* statistics, uint32_t ticker_type,
                       uint64_t count = 1) {
  if (statistics) {
    statistics->recordTick(ticker_type, count);
  }
}

inline void SetTickerCount(Statistics* statistics, uint32_t ticker_type,
                           uint64_t count) {
  if (statistics) {
    statistics->setTickerCount(ticker_type, count);
  }
}

}  // namespace rocksdb

#endif  // YB_ROCKSDB_UTIL_STATISTICS_H
