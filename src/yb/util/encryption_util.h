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

#ifndef YB_UTIL_ENCRYPTION_UTIL_H
#define YB_UTIL_ENCRYPTION_UTIL_H

#include "yb/util/status.h"
#include "yb/util/result.h"
#include "yb/util/header_manager.h"
#include "yb/util/cipher_stream.h"
#include "yb/util/strongly_typed_uuid.h"
#include "yb/util/env.h"

namespace yb {

class EncryptionParamsPB;
class RandomAccessFile;

namespace enterprise {

class HeaderManager;
class BlockAccessCipherStream;
class OpenSSLInitializer;

// Struct generated for encryption status of existing files.
struct FileEncryptionStatus {
  bool is_encrypted;
  uint32_t header_size;
};

// Encryption params consisting of key size 16, 24, or 32, nonce size 12, and counter size 4.
struct EncryptionParams {
  static constexpr uint32_t kBlockSize = 16;
  static constexpr uint32_t kMaxKeySize = 32;

  uint8_t key[kMaxKeySize];
  // Nonce of 12 bytes for encryption.
  uint8_t nonce[kBlockSize - sizeof(uint32_t)];
  // Counter of 4 bytes incremented for each block.
  uint32_t counter;
  uint32_t key_size;

  void ToEncryptionParamsPB(EncryptionParamsPB* encryption_header) const;

  static Result<std::unique_ptr<EncryptionParams>> FromEncryptionParamsPB(
      const EncryptionParamsPB& encryption_header);

  // Given a slice, convert contents to encryption params. Used to read latest universe key.
  static Result<std::unique_ptr<EncryptionParams>> FromSlice(const Slice& s);

  static std::unique_ptr<EncryptionParams> NewEncryptionParams();

  static CHECKED_STATUS IsValidKeySize(uint32_t size);

  bool Equals(const EncryptionParams& other);
};

typedef std::unique_ptr<EncryptionParams> EncryptionParamsPtr;

// Since this will typically be passed in by an external user (YW or yb-admin), UniverseKeyId
// will be a generic string for ease of use.
using UniverseKeyId = std::string;

struct UniverseKeyParams {
  UniverseKeyId version_id;
  EncryptionParamsPtr params;
};

// Thread local buffer for any encryption operations.
class EncryptionBuffer {
 public:
  void* GetBuffer(uint32_t size_needed);
  ~EncryptionBuffer();
  static EncryptionBuffer* Get();
 private:
  uint32_t size = 0;
  void* buffer = nullptr;
};


// Given a readable file, generate a cipher stream and header size for that file.
template <typename BufType, typename Readable>
Result<bool> GetEncryptionInfoFromFile(HeaderManager* header_manager,
                                       Readable* underlying_r,
                                       std::unique_ptr<BlockAccessCipherStream>* stream,
                                       uint32_t* header_size) {
  if (!header_manager) {
    return STATUS(InvalidArgument, "header_manager must be non-null.");
  }

  Slice encryption_info;
  auto metadata_start = header_manager->GetEncryptionMetadataStartIndex();
  auto buf = static_cast<BufType*>(EncryptionBuffer::Get()->GetBuffer(metadata_start));

  RETURN_NOT_OK(underlying_r->Read(0, metadata_start, &encryption_info, buf));
  auto encryption_status = VERIFY_RESULT(
      header_manager->GetFileEncryptionStatusFromPrefix(encryption_info));
  if (!encryption_status.is_encrypted) {
    return false;
  }

  *header_size = metadata_start + encryption_status.header_size;
  buf = static_cast<BufType*>(EncryptionBuffer::Get()->GetBuffer(*header_size));
  RETURN_NOT_OK(underlying_r->Read(
      metadata_start, encryption_status.header_size, &encryption_info, buf));
  auto encryption_params = VERIFY_RESULT(
      header_manager->DecodeEncryptionParamsFromEncryptionMetadata(encryption_info));

  *stream = std::make_unique<BlockAccessCipherStream>(std::move(encryption_params));
  RETURN_NOT_OK((*stream)->Init());
  return true;
}

// Given a writable file, generate a new stream and header for that file.
template <typename Writable>
Status CreateEncryptionInfoForWrite(HeaderManager* header_manager,
                                    Writable* underlying_w,
                                    std::unique_ptr<BlockAccessCipherStream>* stream,
                                    uint32_t* header_size) {
  auto encryption_params = EncryptionParams::NewEncryptionParams();
  string header = VERIFY_RESULT(
      header_manager->SerializeEncryptionParams(*encryption_params.get()));
  RETURN_NOT_OK(underlying_w->Append(header));

  // Since file doesn't exist or this overwrites, append key to the name and create.
  *stream = std::make_unique<BlockAccessCipherStream>(std::move(encryption_params));
  RETURN_NOT_OK((*stream)->Init());
  *header_size = static_cast<uint32_t>(header.size());
  return Status::OK();
}

template <typename EncryptedFile, typename BufType, typename ReadablePtr>
Status CreateRandomAccessFile(ReadablePtr* result,
                              HeaderManager* header_manager,
                              ReadablePtr underlying) {
  result->reset();
  std::unique_ptr<BlockAccessCipherStream> stream;
  uint32_t header_size;

  auto res = GetEncryptionInfoFromFile<BufType>(
      header_manager, underlying.get(), &stream, &header_size);
  bool file_encrypted = VERIFY_RESULT(res);

  if (!file_encrypted) {
    *result = std::move(underlying);
    return Status::OK();
  }

  result->reset(new EncryptedFile(std::move(underlying), std::move(stream), header_size));
  return Status::OK();
}

template <typename EncryptedFile, typename WritablePtr>
Status CreateWritableFile(WritablePtr* result,
                          HeaderManager* header_manager,
                          WritablePtr underlying) {
  result->reset();
  if (!header_manager->IsEncryptionEnabled()) {
    *result = std::move(underlying);
    return Status::OK();
  }

  std::unique_ptr<BlockAccessCipherStream> stream;
  uint32_t header_size = 0;
  RETURN_NOT_OK(CreateEncryptionInfoForWrite(
      header_manager, underlying.get(), &stream, &header_size));
  result->reset(new EncryptedFile(std::move(underlying), std::move(stream), header_size));
  return Status::OK();
}

Result<uint32_t> GetHeaderSize(SequentialFile* file, HeaderManager* header_manager);

OpenSSLInitializer& InitOpenSSL();

} // namespace enterprise
} // namespace yb

#endif // YB_UTIL_ENCRYPTION_UTIL_H
