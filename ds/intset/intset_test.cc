#include "ds/intset/intset.h"

#include <gtest/gtest.h>

#include <limits>
#include <set>
#include <string>
#include <vector>

#include "common/rng.h"

// Blobs recorded from Redis 8.10 by `python3 tools/blob_reference.py` (DUMP of a small
// set of integers embeds its intset as-is).

namespace rd {
namespace {

template <size_t N>
std::string bytes(const char (&lit)[N]) {
  return std::string(lit, N - 1);
}

IntSet make(std::vector<int64_t> values) {
  IntSet s;
  for (int64_t v : values) s.add(v);
  return s;
}

TEST(IntSet, Empty) {
  IntSet s;
  EXPECT_EQ(s.size(), 0u);
  EXPECT_EQ(s.encoding(), 2);
  EXPECT_EQ(s.bytes(), bytes("\002\000\000\000\000\000\000\000"));
  EXPECT_FALSE(s.contains(0));
}

TEST(IntSet, SortedAndUnique) {
  IntSet s = make({5, 1, 3, -2});
  EXPECT_FALSE(s.add(3));
  ASSERT_EQ(s.size(), 4u);
  EXPECT_EQ(s.at(0), -2);
  EXPECT_EQ(s.at(3), 5);
  EXPECT_TRUE(s.contains(1));
  EXPECT_FALSE(s.contains(2));
}

// SADD k 5 1 3 -2
TEST(IntSet, SameBytesAsRedisInt16) {
  EXPECT_EQ(make({5, 1, 3, -2}).bytes(),
            bytes("\002\000\000\000\004\000\000\000\376\377\001\000\003\000\005\000"));
}

// SADD k 1 70000 -2: 70000 doesn't fit in 16 bits; everything becomes 4 bytes.
TEST(IntSet, SameBytesAsRedisInt32) {
  IntSet s = make({1, 70000, -2});
  EXPECT_EQ(s.encoding(), 4);
  EXPECT_EQ(s.bytes(), bytes("\004\000\000\000\003\000\000\000\376\377\377\377\001\000\000\000p\021\001\000"));
}

// SADD k 1 5000000000 -40000
TEST(IntSet, SameBytesAsRedisInt64) {
  IntSet s = make({1, 5000000000, -40000});
  EXPECT_EQ(s.encoding(), 8);
  EXPECT_EQ(s.bytes(), bytes("\010\000\000\000\003\000\000\000\300c\377\377\377\377\377\377\001\000\000\000\000\000\000\000"
                             "\000\362\005*\001\000\000\000"));
}

// SADD k -9223372036854775808 9223372036854775807 0
TEST(IntSet, Extremes) {
  IntSet s = make({std::numeric_limits<int64_t>::min(), std::numeric_limits<int64_t>::max(), 0});
  EXPECT_EQ(s.bytes(), bytes("\010\000\000\000\003\000\000\000\000\000\000\000\000\000\000\200\000\000\000\000\000\000\000\000"
                             "\377\377\377\377\377\377\377\177"));
  EXPECT_TRUE(s.contains(std::numeric_limits<int64_t>::min()));
}

TEST(IntSet, EncodingBoundaries) {
  EXPECT_EQ(make({32767, -32768}).encoding(), 2);
  EXPECT_EQ(make({32768}).encoding(), 4);
  EXPECT_EQ(make({-32769}).encoding(), 4);
  EXPECT_EQ(make({2147483647, -2147483648LL}).encoding(), 4);
  EXPECT_EQ(make({2147483648LL}).encoding(), 8);
}

// The pitfall: a value outside the current encoding can't be in the set; don't truncate
// it and search for the truncated value (70000 as int16 is 4464).
TEST(IntSet, ContainsChecksTheRangeFirst) {
  IntSet s = make({4464});
  EXPECT_FALSE(s.contains(70000));
  EXPECT_FALSE(s.contains(4464 + 65536));
}

TEST(IntSet, RemoveNeverDowngrades) {
  IntSet s = make({1, 70000});
  EXPECT_TRUE(s.remove(70000));
  EXPECT_FALSE(s.remove(70000));
  EXPECT_EQ(s.encoding(), 4);
  EXPECT_EQ(s.size(), 1u);
  EXPECT_EQ(s.bytes(), bytes("\004\000\000\000\001\000\000\000\001\000\000\000"));
}

TEST(IntSet, FromBytesRoundTrip) {
  IntSet s = make({7, -3, 100000});
  auto back = IntSet::from_bytes(s.bytes());
  ASSERT_TRUE(back.has_value());
  EXPECT_EQ(back->bytes(), s.bytes());
  EXPECT_TRUE(back->contains(100000));
}

TEST(IntSet, FromBytesRejectsCorruption) {
  EXPECT_FALSE(IntSet::from_bytes(""));
  EXPECT_FALSE(IntSet::from_bytes(bytes("\003\000\000\000\000\000\000\000")));          // encoding 3
  EXPECT_FALSE(IntSet::from_bytes(bytes("\002\000\000\000\002\000\000\000\001\000")));  // too short
  EXPECT_FALSE(IntSet::from_bytes(bytes("\002\000\000\000\002\000\000\000\002\000\001\000")));  // unsorted
  EXPECT_FALSE(IntSet::from_bytes(bytes("\002\000\000\000\002\000\000\000\001\000\001\000")));  // duplicate
  EXPECT_TRUE(IntSet::from_bytes(bytes("\002\000\000\000\002\000\000\000\001\000\002\000")));
}

// The oracle: std::set<int64_t>.
TEST(IntSet, MatchesAModel) {
  Rng rng(17);
  IntSet s;
  std::set<int64_t> model;
  for (int step = 0; step < 20000; ++step) {
    int bits = rng.coin(0.9) ? 12 : rng.coin(0.5) ? 30 : 62;
    int64_t v = rng.between(-(int64_t{1} << bits), int64_t{1} << bits);
    if (rng.coin(0.6)) {
      ASSERT_EQ(s.add(v), model.insert(v).second);
    } else {
      ASSERT_EQ(s.remove(v), model.erase(v) > 0);
    }
    ASSERT_EQ(s.size(), model.size());
  }
  size_t i = 0;
  for (int64_t v : model) ASSERT_EQ(s.at(i++), v);
  ASSERT_TRUE(IntSet::from_bytes(s.bytes()).has_value());
}

}  // namespace
}  // namespace rd
