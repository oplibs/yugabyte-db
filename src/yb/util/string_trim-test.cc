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

#include "yb/util/string_trim.h"

#include "yb/util/test_macros.h"
#include "yb/util/test_util.h"

namespace yb {
namespace util {

TEST(StringTrimTest, LeftTrimStr) {
  ASSERT_EQ("foo ", LeftTrimStr("  \t \f \n \r \v foo "));
  ASSERT_EQ("oobar", LeftTrimStr("foobar", "fr"));
}

TEST(StringTrimTest, RightTrimStr) {
  ASSERT_EQ(" foo", RightTrimStr(" foo\t \f \n \r \v "));
  ASSERT_EQ("fooba", RightTrimStr("foobar", "fr"));
}

TEST(StringTrimTest, TrimStr) {
  ASSERT_EQ("foo", TrimStr(" \t \f \n \r \v foo \t \f \n \r \v "));
  ASSERT_EQ("ooba", TrimStr("foobar", "fr"));
}

TEST(StringTrimTest, TestApplyEagerLineContinuation) {
  ASSERT_EQ("  foo bar\n  baz  ", ApplyEagerLineContinuation("  foo \\\n      bar\n  baz  "));
  ASSERT_EQ("  foo bar\n  baz  ", ApplyEagerLineContinuation("  foo \\\n   \\\n    bar\n  baz  "));
}

TEST(StringTrimTest, TestTrimLeadingSpaces) {
  ASSERT_STR_EQ_VERBOSE_TRIMMED(
      R"#(
This is my

  Text block
)#",
      LeftShiftTextBlock(R"#(
      This is my

        Text block
      )#"));
}

TEST(StringTrimTest, TestTrimCppComments) {
  ASSERT_EQ(
      R"#(
Line1

Line2
)#",
      TrimCppComments(R"#(
Line1  // This is a comment

Line2  // This is a comment too
)#")
      );
}

}  // namespace util
}  // namespace yb
