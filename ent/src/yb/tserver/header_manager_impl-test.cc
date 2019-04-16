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

#include <sys/types.h>
#include <openssl/rand.h>

#include <string>

#include "yb/tserver/header_manager_impl.h"

#include "yb/util/encryption_util.h"
#include "yb/util/status.h"
#include "yb/util/test_util.h"

#include <glog/logging.h>
#include <gtest/gtest.h>

namespace yb {
namespace tserver {
namespace enterprise {

class TestHeaderManagerImpl : public YBTest {};

TEST_F(TestHeaderManagerImpl, FileOps) {
  std::unique_ptr<yb::enterprise::HeaderManager> header_manager = DefaultHeaderManager();
  auto params = yb::enterprise::EncryptionParams::NewEncryptionParams();
  string header = ASSERT_RESULT(header_manager->SerializeEncryptionParams(*params.get()));
  auto start_idx = header_manager->GetEncryptionMetadataStartIndex();
  auto status = ASSERT_RESULT(header_manager->GetFileEncryptionStatusFromPrefix(
      Slice(header.c_str(), start_idx)));
  ASSERT_TRUE(status.is_encrypted);
  Slice s = Slice(header.c_str() + start_idx, status.header_size);
  auto new_params = ASSERT_RESULT(header_manager->DecodeEncryptionParamsFromEncryptionMetadata(s));
  ASSERT_EQ(memcmp(params->key, new_params->key, new_params->key_size), 0);
  ASSERT_EQ(memcmp(params->nonce, new_params->nonce, 12), 0);
  ASSERT_EQ(params->counter, new_params->counter);
  ASSERT_EQ(params->key_size, new_params->key_size);
}

} // namespace enterprise
} // namespace tserver
} // namespace yb
