// Copyright (c) 2011-present, Facebook, Inc.  All rights reserved.
// This source code is licensed under the BSD-style license found in the
// LICENSE file in the root directory of this source tree. An additional grant
// of patent rights can be found in the PATENTS file in the same directory.
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
#ifndef ROCKSDB_TABLE_INTERNAL_ITERATOR_H
#define ROCKSDB_TABLE_INTERNAL_ITERATOR_H

#pragma once

#include <string>
#include "yb/rocksdb/iterator.h"
#include "yb/rocksdb/status.h"

namespace rocksdb {

class InternalIterator : public Cleanable {
 public:
  InternalIterator() {}
  virtual ~InternalIterator() {}

  // An iterator is either positioned at a key/value pair, or
  // not valid.  This method returns true iff the iterator is valid.
  // It is mandatory to check status() to distinguish between absence of entry vs read error.
  virtual bool Valid() const = 0;

  // Same as Valid(), but returns error if there was a read error.
  // For hot paths consider using Valid() in a loop and checking status after the loop.
  yb::Result<bool> CheckedValid() const {
    return Valid() ? true : (status().ok() ? yb::Result<bool>(false) : status());
  }

  // Position at the first key in the source.  The iterator is Valid()
  // after this call iff the source is not empty.
  virtual void SeekToFirst() = 0;

  // Position at the last key in the source.  The iterator is
  // Valid() after this call iff the source is not empty.
  virtual void SeekToLast() = 0;

  // Position at the first key in the source that at or past target
  // The iterator is Valid() after this call iff the source contains
  // an entry that comes at or past target.
  // target is encoded representation of InternalKey.
  virtual void Seek(const Slice& target) = 0;

  // Moves to the next entry in the source.  After this call, Valid() is
  // true iff the iterator was not positioned at the last entry in the source.
  // REQUIRES: Valid()
  virtual void Next() = 0;

  // Moves to the previous entry in the source.  After this call, Valid() is
  // true iff the iterator was not positioned at the first entry in source.
  // REQUIRES: Valid()
  virtual void Prev() = 0;

  // Return the key for the current entry.  The underlying storage for
  // the returned slice is valid only until the next modification of
  // the iterator.
  // REQUIRES: Valid()
  virtual Slice key() const = 0;

  // Return the value for the current entry.  The underlying storage for
  // the returned slice is valid only until the next modification of
  // the iterator.
  // REQUIRES: !AtEnd() && !AtStart()
  virtual Slice value() const = 0;

  // If an error has occurred, return it.  Else return an ok status.
  // If non-blocking IO is requested and this operation cannot be
  // satisfied without doing some IO, then this returns STATUS(Incomplete, ).
  virtual Status status() const = 0;

  // Make sure that all current and future data blocks used by this iterator
  // will be pinned in memory and will not be released except when
  // ReleasePinnedData() is called or the iterator is deleted.
  virtual Status PinData() { return STATUS(NotSupported, ""); }

  // Release all blocks that were pinned because of PinData() and no future
  // blocks will be pinned.
  virtual Status ReleasePinnedData() { return STATUS(NotSupported, ""); }

  // If true, this means that the Slice returned by key() is valid as long
  // as the iterator is not deleted and ReleasePinnedData() is not called.
  //
  // IsKeyPinned() is guaranteed to always return true if
  //  - PinData() is called
  //  - DB tables were created with BlockBasedTableOptions::use_delta_encoding
  //    set to false.
  virtual bool IsKeyPinned() const { return false; }

  virtual Status GetProperty(std::string prop_name, std::string* prop) {
    return STATUS(NotSupported, "");
  }

 private:
  // No copying allowed
  InternalIterator(const InternalIterator&) = delete;
  InternalIterator& operator=(const InternalIterator&) = delete;
};

// Return an empty iterator (yields nothing).
extern InternalIterator* NewEmptyInternalIterator();

// Return an empty iterator with the specified status.
extern InternalIterator* NewErrorInternalIterator(const Status& status);

}  // namespace rocksdb

#endif // ROCKSDB_TABLE_INTERNAL_ITERATOR_H
