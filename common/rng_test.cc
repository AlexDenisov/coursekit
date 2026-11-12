#include "common/rng.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <numeric>
#include <vector>

namespace rd {
namespace {

TEST(Rng, SameSeedSameNumbers) {
  Rng a(42), b(42), c(43);
  for (int i = 0; i < 100; ++i) {
    uint64_t x = a.next();
    EXPECT_EQ(x, b.next());
    EXPECT_NE(x, c.next());
  }
}

TEST(Rng, KnownSequence) {
  // splitmix64 reference values for seed 0; if these change, every
  // randomized test in the repo changes with them.
  Rng r(0);
  EXPECT_EQ(r.next(), 0xE220A8397B1DCDAFull);
  EXPECT_EQ(r.next(), 0x6E789E6AA1B965F4ull);
}

TEST(Rng, BelowStaysInRangeAndHitsEveryValue) {
  Rng r(1);
  std::vector<int> seen(7);
  for (int i = 0; i < 1000; ++i) {
    uint64_t x = r.below(7);
    ASSERT_LT(x, 7u);
    ++seen[x];
  }
  for (int count : seen) EXPECT_GT(count, 100);  // ~143 each
}

TEST(Rng, BetweenIncludesBothEnds) {
  Rng r(2);
  bool lo = false, hi = false;
  for (int i = 0; i < 1000; ++i) {
    int64_t x = r.between(-2, 2);
    ASSERT_GE(x, -2);
    ASSERT_LE(x, 2);
    lo |= x == -2;
    hi |= x == 2;
  }
  EXPECT_TRUE(lo && hi);
  EXPECT_EQ(r.between(5, 5), 5);
  r.between(INT64_MIN, INT64_MAX);  // full range doesn't overflow
}

TEST(Rng, UniformAndCoin) {
  Rng r(3);
  double sum = 0;
  int heads = 0;
  for (int i = 0; i < 10000; ++i) {
    double u = r.uniform();
    ASSERT_GE(u, 0.0);
    ASSERT_LT(u, 1.0);
    sum += u;
    heads += r.coin(0.25);
  }
  EXPECT_NEAR(sum / 10000, 0.5, 0.02);
  EXPECT_NEAR(heads / 10000.0, 0.25, 0.02);
}

TEST(Rng, ShuffleIsAPermutation) {
  Rng r(4);
  std::vector<int> v(50);
  std::iota(v.begin(), v.end(), 0);
  auto original = v;
  r.shuffle(v.begin(), v.end());
  EXPECT_NE(v, original);
  std::sort(v.begin(), v.end());
  EXPECT_EQ(v, original);
}

}  // namespace
}  // namespace rd
