#include <gtest/gtest.h>

#include "common/rng.h"
#include "db/eviction/eviction.h"

namespace rd {
namespace {

int hits_to_reach(int target, Rng& rng) {
  uint8_t c = 5;
  int hits = 0;
  while (c < target && hits < 10'000'000) {
    c = lfu_log_incr(c, rng);
    ++hits;
  }
  return hits;
}

TEST(Lfu, SaturatesAt255) {
  Rng rng(1);
  EXPECT_EQ(lfu_log_incr(255, rng), 255);
}

TEST(Lfu, FirstHitsAlwaysCount) {
  Rng rng(1);
  EXPECT_EQ(lfu_log_incr(0, rng), 1);
  EXPECT_EQ(lfu_log_incr(5, rng), 6);
}

// Redis's documentation (redis.conf) with lfu-log-factor 10: about 100 hits reach 10,
// 1000 reach 18, 100K reach 142, 1M saturate at 255. Order-of-magnitude checks.
TEST(Lfu, Logarithmic) {
  Rng rng(7);
  int h10 = hits_to_reach(10, rng), h18 = hits_to_reach(18, rng), h142 = hits_to_reach(142, rng);
  EXPECT_GT(h10, 10);
  EXPECT_LT(h10, 1000);
  EXPECT_GT(h18, 200);
  EXPECT_LT(h18, 5000);
  EXPECT_GT(h142, 30'000);
  EXPECT_LT(h142, 300'000);
}

TEST(Lfu, Decay) {
  EXPECT_EQ(lfu_decay(10, 3), 7);
  EXPECT_EQ(lfu_decay(10, 30), 0);
  EXPECT_EQ(lfu_decay(10, 3, 2), 9);
  EXPECT_EQ(lfu_decay(10, 0), 10);
}

}  // namespace
}  // namespace rd
