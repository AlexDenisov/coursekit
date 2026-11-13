#include "db/eviction/eviction.h"

#include <gtest/gtest.h>

#include <memory>
#include <set>
#include <string>
#include <vector>

#include "common/rng.h"
#include "common/transcript.h"

namespace rd {
namespace {

std::string key(int i) { return "k" + std::to_string(i); }

// A keyspace whose "memory" is 100 bytes per key, so maxmemory = 100 * n means "at most
// n keys".
class Eviction : public ::testing::Test {
 protected:
  Evictor make(EvictionPolicy policy, size_t max_keys, size_t samples = 5) {
    return Evictor(ks, rng, {max_keys * 100, policy, samples}, [this] { return ks.size() * 100; });
  }
  void fill(int n) {
    for (int i = 0; i < n; ++i) ks.set(key(i), std::make_unique<StringValue>("v"));
  }
  std::set<int> remaining(int n) {
    std::set<int> out;
    for (int i = 0; i < n; ++i) {
      if (ks.entries().find(key(i))) out.insert(i);
    }
    return out;
  }
  FakeClock clock{1'700'000'000'000};
  Keyspace ks{clock};
  Rng rng{99};
};

TEST(IdleSeconds, Plain) { EXPECT_EQ(idle_seconds(100, 130), 30u); }

// The pitfall: the 24-bit clock wraps every 194 days; "now" can be numerically smaller.
TEST(IdleSeconds, AcrossTheWrap) { EXPECT_EQ(idle_seconds(0xFFFFF0, 0x10), 0x20u); }

TEST(ParsePolicy, Names) {
  EXPECT_EQ(parse_policy("allkeys-lru"), EvictionPolicy::kAllKeysLru);
  EXPECT_EQ(parse_policy("VOLATILE-TTL"), EvictionPolicy::kVolatileTtl);
  EXPECT_EQ(parse_policy("lru"), std::nullopt);
}

TEST_F(Eviction, NoLimitMeansNoWork) {
  fill(100);
  Evictor e(ks, rng, {0, EvictionPolicy::kAllKeysLru, 5}, [this] { return ks.size() * 100; });
  EXPECT_TRUE(e.make_room());
  EXPECT_EQ(ks.size(), 100u);
}

TEST_F(Eviction, UnderTheLimitMeansNoWork) {
  fill(100);
  Evictor e = make(EvictionPolicy::kAllKeysLru, 200);
  EXPECT_TRUE(e.make_room());
  EXPECT_EQ(e.evicted(), 0u);
}

TEST_F(Eviction, NoevictionRefuses) {
  fill(100);
  Evictor e = make(EvictionPolicy::kNoEviction, 50);
  EXPECT_FALSE(e.make_room());
  EXPECT_EQ(ks.size(), 100u);
}

TEST_F(Eviction, AllkeysRandomGetsUnderTheLimit) {
  fill(1000);
  Evictor e = make(EvictionPolicy::kAllKeysRandom, 600);
  EXPECT_TRUE(e.make_room());
  EXPECT_EQ(ks.size(), 600u);
  EXPECT_EQ(e.evicted(), 400u);
}

// Half the keys were read recently; the other half 1000 s ago. LRU evicts the cold ones.
TEST_F(Eviction, AllkeysLruEvictsColdKeys) {
  fill(1000);
  clock.advance(1'000'000);
  for (int i = 500; i < 1000; ++i) ks.find(key(i));  // touch the hot half
  Evictor e = make(EvictionPolicy::kAllKeysLru, 700);
  ASSERT_TRUE(e.make_room());
  auto left = remaining(1000);
  int hot_evicted = 0;
  for (int i = 500; i < 1000; ++i) hot_evicted += left.count(i) ? 0 : 1;
  EXPECT_EQ(left.size(), 700u);
  EXPECT_LE(hot_evicted, 5) << "LRU evicted recently used keys";
}

// Key i was last used at second i. With 10 samples and the pool, approximate LRU is close
// to exact: most of the oldest keys go first.
TEST_F(Eviction, ApproximateLruIsCloseToExact) {
  for (int i = 0; i < 2000; ++i) {
    clock.advance(1000);
    ks.set(key(i), std::make_unique<StringValue>("v"));
  }
  clock.advance(1000);
  Evictor e = make(EvictionPolicy::kAllKeysLru, 1000, 10);
  ASSERT_TRUE(e.make_room());
  auto left = remaining(2000);
  int old_evicted = 0;
  for (int i = 0; i < 1000; ++i) old_evicted += left.count(i) ? 0 : 1;
  EXPECT_GE(old_evicted, 850) << "only " << old_evicted << " of the 1000 oldest keys were evicted";
}

// The pitfall: sampling without a pool. 5 random samples per eviction, forgetting the
// good candidates seen before, does worse than Redis's pool of 16. (Measured with the
// reference: about 360 of the 500 oldest with the pool, about 305 without.)
TEST_F(Eviction, ThePoolRemembersGoodCandidates) {
  for (int i = 0; i < 2000; ++i) {
    clock.advance(1000);
    ks.set(key(i), std::make_unique<StringValue>("v"));
  }
  clock.advance(1000);
  Evictor e = make(EvictionPolicy::kAllKeysLru, 1500, 5);
  ASSERT_TRUE(e.make_room());
  auto left = remaining(2000);
  int oldest_evicted = 0;
  for (int i = 0; i < 500; ++i) oldest_evicted += left.count(i) ? 0 : 1;
  EXPECT_GE(oldest_evicted, 330) << "of the 500 oldest keys only " << oldest_evicted << " were evicted";
}

TEST_F(Eviction, VolatileLruOnlyTouchesKeysWithATtl) {
  fill(100);
  for (int i = 0; i < 30; ++i) ks.set_expire(key(i), clock.now_ms() + 1'000'000);
  Evictor e = make(EvictionPolicy::kVolatileLru, 50);
  EXPECT_FALSE(e.make_room()) << "only 30 keys may be evicted; 70 > 50 remain";
  EXPECT_EQ(ks.size(), 70u);
  for (int i = 30; i < 100; ++i) EXPECT_TRUE(ks.entries().find(key(i))) << key(i);
}

TEST_F(Eviction, VolatileTtlEvictsTheSoonestToExpire) {
  fill(400);
  for (int i = 0; i < 400; ++i) ks.set_expire(key(i), clock.now_ms() + 1000 * (i + 1));
  Evictor e = make(EvictionPolicy::kVolatileTtl, 300, 10);
  ASSERT_TRUE(e.make_room());
  auto left = remaining(400);
  int soonest_evicted = 0;
  for (int i = 0; i < 100; ++i) soonest_evicted += left.count(i) ? 0 : 1;
  EXPECT_GE(soonest_evicted, 75);
}

TEST_F(Eviction, EmptyKeyspaceCantMakeRoom) {
  Evictor e(ks, rng, {10, EvictionPolicy::kAllKeysLru, 5}, [] { return size_t{1000}; });
  EXPECT_FALSE(e.make_room());
}

// Recorded from Redis (tools/cases/eviction.txt): reads and deletes still work when
// writes are refused.
TEST_F(Eviction, WritesGetTheOomError) {
  CommandTable table;
  register_keyspace_commands(table, ks);
  Evictor e(ks, rng, {1, EvictionPolicy::kNoEviction, 5}, [] { return size_t{1000}; });
  auto run = [&](std::vector<std::string> args) {
    std::string reply;
    dispatch_with_eviction(table, e, args, reply);
    return reply;
  };
  EXPECT_EQ(run({"SET", "k", "v"}), "-OOM command not allowed when used memory > 'maxmemory'.\r\n");
  EXPECT_EQ(run({"GET", "k"}), "$-1\r\n");
  EXPECT_EQ(run({"DEL", "k"}), ":0\r\n");
}

TEST_F(Eviction, WritesEvictFirst) {
  CommandTable table;
  register_keyspace_commands(table, ks);
  Evictor e = make(EvictionPolicy::kAllKeysLru, 10);
  for (int i = 0; i < 50; ++i) {
    std::string reply;
    dispatch_with_eviction(table, e, std::vector<std::string>{"SET", key(i), "v"}, reply);
    ASSERT_EQ(reply, "+OK\r\n");
    clock.advance(1000);
  }
  EXPECT_LE(ks.size(), 11u) << "one write may land after the check";
  EXPECT_TRUE(ks.entries().find(key(49))) << "the newest key shouldn't be evicted";
}

}  // namespace
}  // namespace rd
