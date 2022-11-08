// Copyright 2010 Google Inc. All Rights Reserved.
// Authors: gpike@google.com (Geoff Pike), jyrki@google.com (Jyrki Alakuijala)
//
// This file provides a few functions for hashing strings.  On x86-64
// hardware as of early 2010, CityHash64() is much faster than
// MurmurHash64(), and passes the quality-of-hash tests in
// ./hasheval/hasheval_test.cc, among others, with flying colors.  The
// difference in speed can be a factor of two for strings of 50 to 64
// bytes, and sometimes even more for cache-resident longer strings.
//
// CityHash128() is optimized for relatively long strings and returns
// a 128-bit hash.  For strings more than about 2000 bytes it can be
// faster than CityHash64().
//
// Functions in the CityHash family are not suitable for cryptography.
//
// By the way, for some hash functions, given strings a and b, the hash
// of a+b is easily derived from the hashes of a and b.  This property
// doesn't hold for any hash functions in this file.

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
#pragma once

#include <stddef.h>  // for size_t.

#include "yb/gutil/int128.h"
#include "yb/gutil/integral_types.h"

namespace util_hash {

// Hash function for a byte array.
// The mapping may change from time to time.
uint64 CityHash64(const char *buf, size_t len);

// Hash function for a byte array.  For convenience, a 64-bit seed is also
// hashed into the result.  The mapping may change from time to time.
uint64 CityHash64WithSeed(const char *buf, size_t len, uint64 seed);

// Hash function for a byte array.  For convenience, two seeds are also
// hashed into the result.  The mapping may change from time to time.
uint64 CityHash64WithSeeds(const char *buf, size_t len,
                           uint64 seed0, uint64 seed1);

// Hash function for a byte array.  The mapping will never change.
uint128 CityHash128(const char *s, size_t len);

// Hash function for a byte array.  For convenience, a 128-bit seed is also
// hashed into the result.  The mapping will never change.
uint128 CityHash128WithSeed(const char *s, size_t len, uint128 seed);

}  // namespace util_hash
