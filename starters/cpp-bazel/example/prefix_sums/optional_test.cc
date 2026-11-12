#include <gtest/gtest.h>

#include <vector>

#include "example/prefix_sums/prefix_sums.h"

namespace course {
namespace {

TEST(PrefixSumsLowerBound, FindsTheFirstPrefixReachingTheTarget) {
  std::vector<int32_t> a = {3, 1, 4, 1, 5};  // prefixes 0 3 4 8 9 14
  PrefixSums p(a);
  EXPECT_EQ(p.lower_bound(0), 0u);
  EXPECT_EQ(p.lower_bound(4), 2u);
  EXPECT_EQ(p.lower_bound(5), 3u);
  EXPECT_EQ(p.lower_bound(14), 5u);
}

TEST(PrefixSumsLowerBound, UnreachableTargetIsSizePlusOne) {
  std::vector<int32_t> a = {1, 2};
  PrefixSums p(a);
  EXPECT_EQ(p.lower_bound(4), 3u);
}

}  // namespace
}  // namespace course
