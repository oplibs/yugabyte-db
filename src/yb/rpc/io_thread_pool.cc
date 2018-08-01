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

#include "yb/rpc/io_thread_pool.h"

#include <thread>

#include <boost/optional.hpp>
#include <boost/asio/io_service.hpp>

#include <glog/logging.h>

#include "yb/util/format.h"
#include "yb/util/thread.h"

namespace yb {
namespace rpc {

class IoThreadPool::Impl {
 public:
  Impl(const std::string& name, size_t num_threads) : name_(name) {
    threads_.reserve(num_threads);
    size_t index = 0;
    while (threads_.size() != num_threads) {
      threads_.emplace_back([this, index] {
        yb::SetThreadName(Format("iotp_$0_$1", name_, index));
        Execute();
      });
      ++index;
    }
  }

  ~Impl() {
    Shutdown();
    Join();
  }

  IoService& io_service() {
    return io_service_;
  }

  void Shutdown() {
    work_.reset();
  }

  void Join() {
    for (auto& thread : threads_) {
      if (thread.joinable()) {
        thread.join();
      }
    }
  }

 private:
  void Execute() {
    boost::system::error_code ec;
    io_service_.run(ec);
    LOG_IF(ERROR, ec) << "Failed to run io service: " << ec;
  }

  std::string name_;
  std::vector<std::thread> threads_;
  IoService io_service_;
  boost::optional<IoService::work> work_{io_service_};
};

IoThreadPool::IoThreadPool(const std::string& name, size_t num_threads)
    : impl_(new Impl(name, num_threads)) {}

IoThreadPool::~IoThreadPool() {}

IoService& IoThreadPool::io_service() {
  return impl_->io_service();
}

void IoThreadPool::Shutdown() {
  impl_->Shutdown();
}

void IoThreadPool::Join() {
  impl_->Join();
}

} // namespace rpc
} // namespace yb
