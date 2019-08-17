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

#include "yb/docdb/doc_key.h"

#include <memory>

#include "yb/rocksdb/table.h"
#include "yb/rocksdb/table/full_filter_block.h"

#include "yb/docdb/docdb_test_util.h"
#include "yb/gutil/strings/substitute.h"
#include "yb/rocksutil/yb_rocksdb.h"
#include "yb/util/bytes_formatter.h"
#include "yb/util/test_macros.h"
#include "yb/util/test_util.h"

using std::unique_ptr;
using strings::Substitute;
using yb::util::ApplyEagerLineContinuation;
using yb::util::FormatBytesAsStr;
using yb::util::FormatSliceAsStr;
using rocksdb::FilterBitsBuilder;
using rocksdb::FilterBitsReader;

using namespace std::placeholders;

static constexpr int kNumDocOrSubDocKeysPerBatch = 1000;
static constexpr int kNumTestDocOrSubDocKeyComparisons = 10000;

static_assert(kNumDocOrSubDocKeysPerBatch < kNumTestDocOrSubDocKeyComparisons,
              "Number of document/subdocument key pairs to compare must be greater than the "
              "number of random document/subdocument keys to choose from.");
static_assert(kNumTestDocOrSubDocKeyComparisons <
                  kNumDocOrSubDocKeysPerBatch * kNumDocOrSubDocKeysPerBatch, // NOLINT
              "Number of document/subdocument key pairs to compare must be less than the maximum "
              "theoretical number of such pairs given how many keys we generate to choose from.");

namespace yb {
namespace docdb {

// Note on the exact hash value we're using here: 0x4868 should show up as "Hh" in ASCII.
const DocKeyHash kAsciiFriendlyHash = 0x4868;

class DocKeyTest : public YBTest {
 protected:
  vector<SubDocKey> GetVariedSubDocKeys() {
    const int kMaxNumHashKeys = 3;
    const int kMaxNumRangeKeys = 3;
    const int kMaxNumSubKeys = 3;
    vector<SubDocKey> sub_doc_keys;
    for (int num_hash_keys = 0; num_hash_keys <= kMaxNumHashKeys; ++num_hash_keys) {
      for (int num_range_keys = 0; num_range_keys <= kMaxNumRangeKeys; ++num_range_keys) {
        for (int num_sub_keys = 0; num_sub_keys <= kMaxNumSubKeys; ++num_sub_keys) {
          for (int has_hybrid_time = 0; has_hybrid_time <= 1; ++has_hybrid_time) {
            SubDocKey sub_doc_key;
            if (num_hash_keys > 0) {
              sub_doc_key.doc_key().set_hash(kAsciiFriendlyHash);
            }
            for (int hIndex = 0; hIndex < num_hash_keys; ++hIndex) {
              sub_doc_key.doc_key().hashed_group().push_back(
                  PrimitiveValue(Format("h$0_$1", hIndex, std::string(hIndex + 1, 'h'))));
            }
            for (int rIndex = 0; rIndex < num_range_keys; ++rIndex) {
              sub_doc_key.doc_key().range_group().push_back(
                  PrimitiveValue(Format("r$0_$1", rIndex, std::string(rIndex + 1, 'r'))));
            }
            for (int skIndex = 0; skIndex < num_sub_keys; ++skIndex) {
              sub_doc_key.subkeys().push_back(
                  PrimitiveValue(Format("sk$0_$1", skIndex, std::string(skIndex + 1, 's'))));
            }
            if (has_hybrid_time) {
              sub_doc_key.set_hybrid_time(DocHybridTime(HybridTime::FromMicros(123456), 1));
            }
            sub_doc_keys.push_back(sub_doc_key);
          }
        }
      }
    }
    return sub_doc_keys;
  }

  string GetTestDescriptionForSubDocKey(const SubDocKey& sub_doc_key) {
    return yb::Format(
        "Encoded SubDocKey: $0; encoded input binary data: $1",
        sub_doc_key,
        sub_doc_key.Encode().AsSlice().ToDebugHexString()
    );
  }

};

namespace {

int Sign(int x) {
  if (x < 0) return -1;
  if (x > 0) return 1;
  return 0;
}

template<typename T>
std::vector<T> GenRandomDocOrSubDocKeys(RandomNumberGenerator* rng,
                                        UseHash use_hash,
                                        int num_keys);

template<>
std::vector<DocKey> GenRandomDocOrSubDocKeys<DocKey>(RandomNumberGenerator* rng,
                                                     UseHash use_hash,
                                                     int num_keys) {
  return GenRandomDocKeys(rng, use_hash, num_keys);
}

template<>
std::vector<SubDocKey> GenRandomDocOrSubDocKeys<SubDocKey>(RandomNumberGenerator* rng,
                                                           UseHash use_hash,
                                                           int num_keys) {
  return GenRandomSubDocKeys(rng, use_hash, num_keys);
}

template <typename DocOrSubDocKey>
void TestRoundTripDocOrSubDocKeyEncodingDecoding(const DocOrSubDocKey& doc_or_subdoc_key) {
  KeyBytes encoded_key = doc_or_subdoc_key.Encode();
  DocOrSubDocKey decoded_key;
  ASSERT_OK(decoded_key.FullyDecodeFrom(encoded_key.AsSlice()));
  ASSERT_EQ(doc_or_subdoc_key, decoded_key);
  KeyBytes reencoded_doc_key = decoded_key.Encode();
  ASSERT_EQ(encoded_key.ToString(), reencoded_doc_key.ToString());
}

template <typename DocOrSubDocKey>
void TestRoundTripDocOrSubDocKeyEncodingDecoding() {
  RandomNumberGenerator rng;  // Use the default seed to keep it deterministic.
  for (auto use_hash : UseHash::kValues) {
    auto doc_or_subdoc_keys = GenRandomDocOrSubDocKeys<DocOrSubDocKey>(
        &rng, use_hash, kNumDocOrSubDocKeysPerBatch);
    for (const auto& doc_or_subdoc_key : doc_or_subdoc_keys) {
      TestRoundTripDocOrSubDocKeyEncodingDecoding(doc_or_subdoc_key);
    }
  }
}

template <typename DocOrSubDocKey>
void TestDocOrSubDocKeyComparison() {
  RandomNumberGenerator rng;  // Use the default seed to keep it deterministic.
  for (auto use_hash : UseHash::kValues) {
    auto keys = GenRandomDocOrSubDocKeys<DocOrSubDocKey>(
        &rng, use_hash, kNumDocOrSubDocKeysPerBatch);
    for (int k = 0; k < kNumTestDocOrSubDocKeyComparisons; ++k) {
      const auto& a = keys[rng() % keys.size()];
      const auto& b = keys[rng() % keys.size()];
      ASSERT_EQ(a == b, !(a != b));
      ASSERT_EQ(a == b, a.ToString() == b.ToString());

      const int object_comparison = a.CompareTo(b);
      const int reverse_object_comparison = b.CompareTo(a);

      const KeyBytes a_encoded = a.Encode();
      const KeyBytes b_encoded = b.Encode();
      const int encoded_comparison = a_encoded.CompareTo(b_encoded);
      const int reverse_encoded_comparison = b_encoded.CompareTo(a_encoded);

      ASSERT_EQ(Sign(object_comparison), Sign(encoded_comparison))
          << "Object comparison inconsistent with encoded byte sequence comparison:\n"
          << "a: " << a.ToString() << "\n"
          << "b: " << b.ToString() << "\n"
          << "a.Encode(): " << a.Encode().ToString() << "\n"
          << "b.Encode(): " << b.Encode().ToString() << "\n"
          << "a.CompareTo(b): " << object_comparison << "\n"
          << "a.Encode().CompareTo(b.Encode()): " << encoded_comparison;
      ASSERT_EQ(0, object_comparison + reverse_object_comparison);
      ASSERT_EQ(0, encoded_comparison + reverse_encoded_comparison);
    }
  }
}

}  // unnamed namespace

TEST_F(DocKeyTest, TestDocKeyToString) {
  ASSERT_EQ(
      "DocKey([], [10, \"foo\", 20, \"bar\"])",
      DocKey(PrimitiveValues(10, "foo", 20, "bar")).ToString());
  ASSERT_EQ(
      "DocKey(0x1234, "
      "[\"hashed_key1\", 123, \"hashed_key2\", 234], [10, \"foo\", 20, \"bar\"])",
      DocKey(0x1234,
             PrimitiveValues("hashed_key1", 123, "hashed_key2", 234),
             PrimitiveValues(10, "foo", 20, "bar")).ToString());
}

TEST_F(DocKeyTest, TestSubDocKeyToString) {
  ASSERT_EQ(
      "SubDocKey(DocKey([], [\"range_key1\", 1000, \"range_key_3\"]), [HT{ physical: 12345 }])",
      SubDocKey(DocKey(PrimitiveValues("range_key1", 1000, "range_key_3")),
                HybridTime::FromMicros(12345L)).ToString());
  ASSERT_EQ(
      "SubDocKey(DocKey([], [\"range_key1\", 1000, \"range_key_3\"]), "
      "[\"subkey1\"; HT{ physical: 20000 }])",
      SubDocKey(
          DocKey(PrimitiveValues("range_key1", 1000, "range_key_3")),
          PrimitiveValue("subkey1"), HybridTime::FromMicros(20000L)
      ).ToString());

}

TEST_F(DocKeyTest, TestDocKeyEncoding) {
  // A few points to make it easier to understand the expected binary representations here:
  // - Initial bytes such as 'S', 'I' correspond the ValueType enum.
  // - Strings are terminated with \x00\x00.
  // - Groups of key components in the document key ("hashed" and "range" components) are separated
  //   with '!'.
  // - 64-bit signed integers are encoded using big-endian format with sign bit inverted.
  ASSERT_STR_EQ_VERBOSE_TRIMMED(
      ApplyEagerLineContinuation(
          R"#(
              "Sval1\x00\x00\
               I\x80\x00\x00\x00\x00\x00\x03\xe8\
               Sval2\x00\x00\
               I\x80\x00\x00\x00\x00\x00\x07\xd0\
               !"
          )#"),
      FormatBytesAsStr(DocKey(PrimitiveValues("val1", 1000, "val2", 2000)).Encode().data()));

  InetAddress addr;
  ASSERT_OK(addr.FromString("1.2.3.4"));

  // To get a descending sorting, we store the negative of a decimal type. 100.2 gets converted to
  // -100.2 which in the encoded form is equal to \x1c\xea\xfe\xd7.
  ASSERT_STR_EQ_VERBOSE_TRIMMED(
      ApplyEagerLineContinuation(
          R"#(
            "a\x89\x9e\x93\xce\xff\xff\
             I\x80\x00\x00\x00\x00\x00\x03\xe8\
             b\x7f\xff\xff\xff\xff\xff\xfc\x17\
             a\x89\x9e\x93\xce\xff\xfe\xff\xff\
             .\xfe\xfd\xfc\xfb\xff\xff\
             c\x7f\xff\xff\xff\xff\xff\xfc\x17\
             d\x1c\xea\xfe\xd7\
             E\xdd\x14\
             !"
          )#"),
      FormatBytesAsStr(DocKey({
          PrimitiveValue("val1", SortOrder::kDescending),
          PrimitiveValue(1000),
          PrimitiveValue(1000, SortOrder::kDescending),
          PrimitiveValue(BINARY_STRING("val1""\x00"), SortOrder::kDescending),
          PrimitiveValue(addr, SortOrder::kDescending),
          PrimitiveValue(Timestamp(1000), SortOrder::kDescending),
          PrimitiveValue::Decimal(util::Decimal("100.02").EncodeToComparable(),
                                  SortOrder::kDescending),
          PrimitiveValue::Decimal(util::Decimal("0.001").EncodeToComparable(),
                                  SortOrder::kAscending),
                              }).Encode().data()));

  ASSERT_STR_EQ_VERBOSE_TRIMMED(
      ApplyEagerLineContinuation(
          R"#("G\
               \xca\xfe\
               Shashed1\x00\x00\
               Shashed2\x00\x00\
               !\
               Srange1\x00\x00\
               I\x80\x00\x00\x00\x00\x00\x03\xe8\
               Srange2\x00\x00\
               I\x80\x00\x00\x00\x00\x00\x07\xd0\
               !")#"),
      FormatBytesAsStr(DocKey(
          0xcafe,
          PrimitiveValues("hashed1", "hashed2"),
          PrimitiveValues("range1", 1000, "range2", 2000)).Encode().data()));
}

TEST_F(DocKeyTest, TestBasicSubDocKeyEncodingDecoding) {
  const SubDocKey subdoc_key(DocKey({PrimitiveValue("some_doc_key")}),
                             PrimitiveValue("sk1"),
                             PrimitiveValue("sk2"),
                             PrimitiveValue(BINARY_STRING("sk3""\x00"), SortOrder::kDescending),
                             HybridTime::FromMicros(1000));
  const KeyBytes encoded_subdoc_key(subdoc_key.Encode());
  ASSERT_STR_EQ_VERBOSE_TRIMMED(
      ApplyEagerLineContinuation(
          R"#("Ssome_doc_key\x00\x00\
               !\
               Ssk1\x00\x00\
               Ssk2\x00\x00\
               a\x8c\x94\xcc\xff\xfe\xff\xff\
               #\x80\xff\x05T=\xf7)\xbc\x18\x80K"
          )#"
      ),
      encoded_subdoc_key.ToString()
  );
  SubDocKey decoded_subdoc_key;
  ASSERT_OK(decoded_subdoc_key.FullyDecodeFrom(encoded_subdoc_key.AsSlice()));
  ASSERT_EQ(subdoc_key, decoded_subdoc_key);
  Slice source = encoded_subdoc_key.data();
  boost::container::small_vector<Slice, 20> slices;
  ASSERT_OK(SubDocKey::PartiallyDecode(&source, &slices));
  const DocKey& dockey = subdoc_key.doc_key();
  const auto& range_group = dockey.range_group();
  size_t size = slices.size();
  ASSERT_EQ(range_group.size() + 1, size);
  --size; // the last one is time
  for (size_t i = 0; i != size; ++i) {
    PrimitiveValue value;
    Slice temp = slices[i];
    ASSERT_OK(value.DecodeFromKey(&temp));
    ASSERT_TRUE(temp.empty());
    ASSERT_EQ(range_group[i], value);
  }
  DocHybridTime time;
  Slice temp = slices[size];
  ASSERT_OK(time.DecodeFrom(&temp));
  ASSERT_TRUE(temp.empty());
  ASSERT_EQ(subdoc_key.doc_hybrid_time(), time);
}

TEST_F(DocKeyTest, TestRandomizedDocKeyRoundTripEncodingDecoding) {
  TestRoundTripDocOrSubDocKeyEncodingDecoding<DocKey>();
}

TEST_F(DocKeyTest, TestRandomizedSubDocKeyRoundTripEncodingDecoding) {
  TestRoundTripDocOrSubDocKeyEncodingDecoding<SubDocKey>();
}

TEST_F(DocKeyTest, TestDocKeyComparison) {
  TestDocOrSubDocKeyComparison<DocKey>();
}

TEST_F(DocKeyTest, TestSubDocKeyComparison) {
  TestDocOrSubDocKeyComparison<SubDocKey>();
}

TEST_F(DocKeyTest, TestSubDocKeyStartsWith) {
  RandomNumberGenerator rng;  // Use the default seed to keep it deterministic.
  auto subdoc_keys = GenRandomSubDocKeys(&rng, UseHash::kFalse, 1000);
  for (const auto& subdoc_key : subdoc_keys) {
    if (subdoc_key.num_subkeys() > 0) {
      const SubDocKey doc_key_only = SubDocKey(subdoc_key.doc_key());
      const SubDocKey doc_key_only_with_ht =
          SubDocKey(subdoc_key.doc_key(), subdoc_key.hybrid_time());
      ASSERT_TRUE(subdoc_key.StartsWith(doc_key_only));
      ASSERT_FALSE(doc_key_only.StartsWith(subdoc_key));
      SubDocKey with_another_doc_gen_ht(subdoc_key);
      with_another_doc_gen_ht.set_hybrid_time(
          DocHybridTime(subdoc_key.hybrid_time().ToUint64() + 1, 0, kMinWriteId));
      ASSERT_FALSE(with_another_doc_gen_ht.StartsWith(doc_key_only_with_ht));
      ASSERT_FALSE(with_another_doc_gen_ht.StartsWith(subdoc_key));
      ASSERT_FALSE(subdoc_key.StartsWith(with_another_doc_gen_ht));
    }
  }
}

std::string EncodeSubDocKey(const std::string& hash_key,
    const std::string& range_key, const std::string& sub_key, uint64_t time) {
  DocKey dk(DocKey(0, PrimitiveValues(hash_key), PrimitiveValues(range_key)));
  return SubDocKey(dk, PrimitiveValue(sub_key),
      HybridTime::FromMicros(time)).Encode().AsStringRef();
}

std::string EncodeSimpleSubDocKey(const std::string& hash_key) {
  return EncodeSubDocKey(hash_key, "range_key", "sub_key", 12345L);
}

std::string EncodeSimpleSubDocKeyWithDifferentNonHashPart(const std::string& hash_key) {
  return EncodeSubDocKey(hash_key, "another_range_key", "another_sub_key", 55555L);
}

TEST_F(DocKeyTest, TestKeyMatching) {
  DocDbAwareFilterPolicy policy(rocksdb::FilterPolicy::kDefaultFixedSizeFilterBits, nullptr);
  std::string keys[] = { "foo", "bar", "test" };
  std::string absent_key = "fake";

  std::unique_ptr<FilterBitsBuilder> builder(policy.GetFilterBitsBuilder());
  ASSERT_NE(builder, nullptr);
  // Policy supports GetFilterBitsBuilder/Reader interface (see description in filter_policy.h) -
  // lets test it.
  for (const auto& key : keys) {
    builder->AddKey(policy.GetKeyTransformer()->Transform(EncodeSimpleSubDocKey(key)));
  }
  std::unique_ptr<const char[]> buf;
  rocksdb::Slice filter = builder->Finish(&buf);

  std::unique_ptr<FilterBitsReader> reader(policy.GetFilterBitsReader(filter));

  auto may_match = [&](const std::string& sub_doc_key_str) {
    return reader->MayMatch(policy.GetKeyTransformer()->Transform(sub_doc_key_str));
  };

  for (const auto &key : keys) {
    ASSERT_TRUE(may_match(EncodeSimpleSubDocKey(key))) << "Key: " << key;
    ASSERT_TRUE(may_match(EncodeSimpleSubDocKeyWithDifferentNonHashPart(key))) << "Key: " << key;
  }
  ASSERT_FALSE(may_match(EncodeSimpleSubDocKey(absent_key))) << "Key: " << absent_key;
}

TEST_F(DocKeyTest, TestWriteId) {
  SubDocKey subdoc_key(DocKey({PrimitiveValue("a"), PrimitiveValue(135)}),
                       DocHybridTime(1000000, 4091, 135));
  TestRoundTripDocOrSubDocKeyEncodingDecoding(subdoc_key);
}

struct CollectedIntent {
  IntentStrength strength;
  KeyBytes intent_key;
  Slice value;

  std::string ToString() const {
    return Format("{ strength: $0 intent_key: $1 value: $2 }",
                  strength, SubDocKey::DebugSliceToString(intent_key.AsSlice()),
                  value.ToDebugHexString());
  }
};

class IntentCollector {
 public:
  explicit IntentCollector(std::vector<CollectedIntent>* out) : out_(out) {}

  Status operator()(IntentStrength strength, Slice value, KeyBytes* key) {
    out_->push_back({strength, *key, value});
    return Status::OK();
  }

 private:
  std::vector<CollectedIntent>* out_;
};

TEST_F(DocKeyTest, TestDecodePrefixLengths) {
  for (const auto& sub_doc_key : GetVariedSubDocKeys()) {
    std::vector<PrimitiveValue> subkeys;
    const auto encoded_input = sub_doc_key.Encode();
    const string test_description = GetTestDescriptionForSubDocKey(sub_doc_key);
    SCOPED_TRACE(test_description);
    const Slice subdockey_slice = encoded_input.AsSlice();
    const auto encoded_sizes = ASSERT_RESULT(DocKey::EncodedHashPartAndDocKeySizes(
        subdockey_slice, AllowSpecial::kFalse));
    const size_t encoded_hash_key_size = encoded_sizes.first;
    const size_t encoded_doc_key_size = encoded_sizes.second;

    size_t expected_hash_enc_size = 0;
    const DocKey& doc_key = sub_doc_key.doc_key();
    if (!doc_key.hashed_group().empty()) {
      // We subtract 1 because we don't want to include kGroupEnd in the expected hash key size.
      expected_hash_enc_size = DocKey(doc_key.hash(), doc_key.hashed_group()).Encode().size() - 1;
    }
    EXPECT_EQ(expected_hash_enc_size, encoded_hash_key_size);
    EXPECT_EQ(sub_doc_key.doc_key().Encode().size(), encoded_doc_key_size);

    SubDocKey cur_key;
    boost::container::small_vector<size_t, 8> prefix_lengths;
    std::vector<size_t> expected_prefix_lengths;
    if (doc_key.has_hash()) {
      cur_key.doc_key() = DocKey(doc_key.hash(), doc_key.hashed_group());
      // Subtract one to avoid counting the final kGroupEnd, unless this is the entire key.
      if (doc_key.range_group().empty()) {
        expected_prefix_lengths.push_back(cur_key.Encode().size());
      } else {
        expected_prefix_lengths.push_back(cur_key.Encode().size() - 1);
      }
    }
    const size_t num_range_keys = doc_key.range_group().size();
    for (size_t i = 0; i < num_range_keys; ++i) {
      cur_key.doc_key().range_group().push_back(doc_key.range_group()[i]);
      if (i < num_range_keys - 1) {
        expected_prefix_lengths.push_back(cur_key.Encode().size() - 1);
      } else {
        // Only cound the final kGroupEnd for the last range key.
        expected_prefix_lengths.push_back(cur_key.Encode().size());
      }
    }

    for (const auto& subkey : sub_doc_key.subkeys()) {
      cur_key.subkeys().push_back(subkey);
      expected_prefix_lengths.push_back(cur_key.Encode().size());
    }

    EXPECT_OK(SubDocKey::DecodePrefixLengths(subdockey_slice, &prefix_lengths));
    EXPECT_EQ(yb::ToString(expected_prefix_lengths), yb::ToString(prefix_lengths));
  }
}

TEST_F(DocKeyTest, TestEnumerateIntents) {
  for (const auto& sub_doc_key : GetVariedSubDocKeys()) {
    for (auto partial_range_key_intents : PartialRangeKeyIntents::kValues) {
      const auto encoded_input = sub_doc_key.Encode();
      const string test_description = Format(
          "$0. Partial range key intents: $1",
          GetTestDescriptionForSubDocKey(sub_doc_key),
          partial_range_key_intents ? "yes" : "no");
      SCOPED_TRACE(test_description);
      const Slice subdockey_slice = encoded_input.AsSlice();

      vector<CollectedIntent> collected_intents;
      vector<CollectedIntent> collected_intents_old;
      KeyBytes encoded_key_buffer;

      VLOG(1) << "EnumerateIntents for: " << test_description;
      EXPECT_OK(EnumerateIntents(
          subdockey_slice,
          /* value */ Slice("some_value"),
          IntentCollector(&collected_intents),
          &encoded_key_buffer,
          partial_range_key_intents));

      if (VLOG_IS_ON(1)) {
        for (const auto& intent : collected_intents) {
          auto intent_slice = intent.intent_key.AsSlice();

          VLOG(1) << "Found intent: " << SubDocKey::DebugSliceToString(intent_slice)
                  << ", raw bytes: " << FormatSliceAsStr(intent_slice)
                  << ", strength: " << intent.strength;
        }
      }

      std::vector<SubDocKey> expected_intents;
      SubDocKey current_expected_intent;

      expected_intents.push_back(SubDocKey());

      if (!sub_doc_key.doc_key().hashed_group().empty()) {
        current_expected_intent = SubDocKey(DocKey(
            sub_doc_key.doc_key().hash(),
            sub_doc_key.doc_key().hashed_group()));
        expected_intents.push_back(current_expected_intent);
      }

      const auto& range_group = sub_doc_key.doc_key().range_group();
      for (size_t range_idx = 0; range_idx < range_group.size(); ++range_idx) {
        current_expected_intent.doc_key().range_group().push_back(range_group[range_idx]);
        if (partial_range_key_intents || range_idx == range_group.size() - 1) {
          expected_intents.push_back(current_expected_intent);
        }
      }

      if (!sub_doc_key.doc_key().empty()) {
        for (const auto& subkey : sub_doc_key.subkeys()) {
          current_expected_intent.AppendSubKey(subkey);
          expected_intents.push_back(current_expected_intent);
        }
      }

      {
        std::set<SubDocKey> expected_intents_set(expected_intents.begin(), expected_intents.end());
        SCOPED_TRACE(Format("Expected intents: $0", yb::ToString(expected_intents_set)));

        // There should be no duplicate intents in our set of expected intents.
        EXPECT_EQ(expected_intents_set.size(), expected_intents.size());

        const SubDocKey doc_key_only(sub_doc_key.doc_key());
        EXPECT_TRUE(expected_intents_set.count(doc_key_only))
            << "doc_key_only: " << doc_key_only;

        SubDocKey hash_part_only(sub_doc_key);
        hash_part_only.subkeys().clear();
        hash_part_only.doc_key().range_group().clear();
        hash_part_only.remove_hybrid_time();
        EXPECT_TRUE(expected_intents_set.count(hash_part_only))
            << "hash_part_only: " << hash_part_only;
      }

      EXPECT_EQ(expected_intents.size(), collected_intents.size())
          << "Expected: " << yb::ToString(expected_intents)
          << ", collected: " << yb::ToString(collected_intents);
      const size_t num_intents = std::min(expected_intents.size(), collected_intents.size());
      for (size_t i = 0; i < num_intents; ++i) {
        SubDocKey decoded_intent_key;
        const auto& intent = collected_intents[i];
        Slice intent_key_slice = intent.intent_key.AsSlice();
        EXPECT_OK(decoded_intent_key.DecodeFrom(&intent_key_slice, HybridTimeRequired::kFalse));
        EXPECT_EQ(0, intent_key_slice.size());
        EXPECT_FALSE(decoded_intent_key.has_hybrid_time());
        EXPECT_EQ(expected_intents[i].ToString(), decoded_intent_key.ToString());

        SubDocKey a = expected_intents[i];
        SubDocKey b = decoded_intent_key;
        VLOG(1) << "Doc key matches: " << (a.doc_key() == b.doc_key());
        VLOG(1) << "has_hybrid_time matches: " << (a.has_hybrid_time() == b.has_hybrid_time());
        if (a.has_hybrid_time() && b.has_hybrid_time()) {
          VLOG(1) << "HT matches: " << (a.hybrid_time() == b.hybrid_time());
        }
        VLOG(1) << "Subkeys match: " << (a.subkeys() == b.subkeys());

        EXPECT_EQ(expected_intents[i], decoded_intent_key);
        if (i < num_intents - 1) {
          EXPECT_EQ(IntentStrength::kWeak, intent.strength);
          EXPECT_EQ(0, intent.value.size());
        } else {
          EXPECT_EQ(IntentStrength::kStrong, intent.strength);
          EXPECT_GT(intent.value.size(), 0);
        }
      }
    }
  }
}

}  // namespace docdb
}  // namespace yb
