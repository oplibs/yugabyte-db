//
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
//

#pragma once

#include "yb/gutil/ref_counted.h"

namespace yb {

class AtomicMillisLag;
class Counter;
class CounterPrototype;
class Gauge;
class Histogram;
class HistogramPrototype;
class HistogramSnapshotPB;
class HdrHistogram;
class Metric;
class MetricEntityPrototype;
class MetricPrototype;
class MetricRegistry;
class MillisLag;
class MillisLagPrototype;
class NMSWriter;
class PrometheusWriter;

struct MetricJsonOptions;
struct MetricPrometheusOptions;

class MetricEntity;
using MetricEntityPtr = scoped_refptr<MetricEntity>;

template<typename T>
class AtomicGauge;
template<typename T>
class FunctionGauge;
template<typename T>
class GaugePrototype;

} // namespace yb
