#include "db/glob/glob.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <string>
#include <utility>
#include <vector>

namespace rd {
namespace {

// The oracle: KEYS <pattern> on real Redis over every subject string
// (`python3 tools/glob_reference.py > db/glob/redis_cases.inc`).
#include "db/glob/redis_cases.inc"

TEST(Glob, Literal) {
  EXPECT_TRUE(glob_match("hello", "hello"));
  EXPECT_FALSE(glob_match("hello", "hell"));
  EXPECT_FALSE(glob_match("hell", "hello"));
  EXPECT_TRUE(glob_match("", ""));
  EXPECT_FALSE(glob_match("", "a"));
}

TEST(Glob, Star) {
  EXPECT_TRUE(glob_match("*", "a"));
  EXPECT_TRUE(glob_match("h*llo", "hllo"));
  EXPECT_TRUE(glob_match("h*llo", "heeeello"));
  EXPECT_TRUE(glob_match("user:*", "user:"));
  EXPECT_TRUE(glob_match("a**c", "abc"));
  EXPECT_FALSE(glob_match("a*c", "ab"));
}

// Redis's quirk: the matcher never matches an empty string against a non-empty
// pattern, not even "*" (KEYS * only matches the empty key thanks to a fast path).
TEST(Glob, NothingMatchesTheEmptyString) {
  EXPECT_FALSE(glob_match("*", ""));
  EXPECT_FALSE(glob_match("**", ""));
}

TEST(Glob, QuestionMarkIsExactlyOneByte) {
  EXPECT_TRUE(glob_match("h?llo", "hello"));
  EXPECT_FALSE(glob_match("h?llo", "hllo"));
  EXPECT_FALSE(glob_match("?", ""));
}

TEST(Glob, Sets) {
  EXPECT_TRUE(glob_match("h[ae]llo", "hallo"));
  EXPECT_FALSE(glob_match("h[ae]llo", "hxllo"));
  EXPECT_TRUE(glob_match("h[^e]llo", "hallo"));
  EXPECT_FALSE(glob_match("h[^e]llo", "hello"));
  EXPECT_TRUE(glob_match("user:[0-9]*", "user:12"));
  EXPECT_TRUE(glob_match("h[b-a]llo", "hallo"));  // reversed range
}

TEST(Glob, Escapes) {
  EXPECT_TRUE(glob_match("a\\*c", "a*c"));
  EXPECT_FALSE(glob_match("a\\*c", "abc"));
  EXPECT_TRUE(glob_match("a[\\]]c", "a]c"));
  EXPECT_TRUE(glob_match("a\\\\c", "a\\c"));
}

TEST(Glob, BinarySafe) {
  EXPECT_TRUE(glob_match(std::string_view("a*", 2), std::string_view("a\0b", 3)));
  EXPECT_TRUE(glob_match(std::string_view("a?b", 3), std::string_view("a\0b", 3)));
}

// Every pattern, every subject, as real Redis decides.
TEST(Glob, MatchesRedisOnEveryCase) {
  int checked = 0;
  for (const auto& [pattern, matching] : kRedisCases) {
    for (const std::string& s : kSubjects) {
      bool want = std::find(matching.begin(), matching.end(), s) != matching.end();
      ASSERT_EQ(glob_match(pattern, s), want)
          << "pattern " << ::testing::PrintToString(pattern) << ", string "
          << ::testing::PrintToString(s);
      ++checked;
    }
  }
  EXPECT_GT(checked, 10000);
}

// The pitfall: naive backtracking tries every way to split 60 a's among 25 stars
// (about 10^17 ways). Redis answers instantly; so must you.
TEST(Glob, ManyStarsDoNotBacktrackExponentially) {
  std::string pattern;
  for (int i = 0; i < 25; ++i) pattern += "a*";
  pattern += "b";
  std::string s(60, 'a');
  auto start = std::chrono::steady_clock::now();
  EXPECT_FALSE(glob_match(pattern, s));
  EXPECT_TRUE(glob_match(pattern, s + "b"));
  auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - start).count();
  EXPECT_LT(ms, 1000);
}

}  // namespace
}  // namespace rd
