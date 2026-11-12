#include "example/prefix_sums/prefix_sums.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <limits>
#include <vector>

#include "common/rng.h"

namespace course {
namespace {

TEST(PrefixSums, SmallExampleFromTheReadme) {
  std::vector<int32_t> a = {3, 1, 4, 1, 5};
  PrefixSums p(a);
  EXPECT_EQ(p.size(), 5u);
  EXPECT_EQ(p.sum(1, 4), 6);
  EXPECT_EQ(p.sum(0, 5), 14);
}

TEST(PrefixSums, EmptyRangeIsZero) {
  std::vector<int32_t> a = {7, 8};
  PrefixSums p(a);
  EXPECT_EQ(p.sum(0, 0), 0);
  EXPECT_EQ(p.sum(2, 2), 0);
}

TEST(PrefixSums, EmptyArray) {
  PrefixSums p(std::span<const int32_t>{});
  EXPECT_EQ(p.size(), 0u);
  EXPECT_EQ(p.sum(0, 0), 0);
}

TEST(PrefixSums, LargeValuesDoNotOverflow) {
  std::vector<int32_t> a(100'000, std::numeric_limits<int32_t>::max());
  PrefixSums p(a);
  EXPECT_EQ(p.sum(0, a.size()), int64_t{100'000} * std::numeric_limits<int32_t>::max());
}

// The oracle: a plain loop, written here so the test never trusts the code under test.
TEST(PrefixSums, MatchesBruteForceOnRandomArrays) {
  Rng rng(42);
  for (int round = 0; round < 50; ++round) {
    std::vector<int32_t> a(rng.below(40));
    for (auto& x : a) x = static_cast<int32_t>(rng.between(-1000, 1000));
    PrefixSums p(a);
    for (size_t l = 0; l <= a.size(); ++l) {
      int64_t want = 0;
      for (size_t r = l; r <= a.size(); ++r) {
        ASSERT_EQ(p.sum(l, r), want) << "l = " << l << ", r = " << r << ", round " << round;
        if (r < a.size()) want += a[r];
      }
    }
  }
}

}  // namespace
}  // namespace course
