#include "server/lean_redis/lean_redis.h"

#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "common/test_client.h"
#include "common/transcript.h"

namespace rd {
namespace {

using testing::kAnyOrder;
using testing::kAnyPairOrder;
using testing::kExact;
using testing::Order;
using namespace std::string_literals;

struct CompatStep {
  std::vector<std::string> args;
  std::string reply;
  Order order;
};
struct CompatGroup {
  std::string name;
  std::vector<CompatStep> steps;
};

// `python3 tools/redis_reference.py --inc tools/cases/zsets.txt > server/lean_redis/compat_cases.inc`
#include "server/lean_redis/compat_cases.inc"

class LeanRedisTest : public ::testing::Test {
 protected:
  void start(LeanRedisOptions options = {}) {
    server_ = std::make_unique<LeanRedis>(clock_, options);
    port_ = server_->listen("127.0.0.1", 0);
    ASSERT_NE(port_, 0);
    thread_ = std::thread([this] { server_->run(); });
  }
  void TearDown() override {
    if (server_) server_->stop();
    if (thread_.joinable()) thread_.join();
  }
  SystemClock clock_;
  std::unique_ptr<LeanRedis> server_;
  uint16_t port_ = 0;
  std::thread thread_;
};

// Sorted sets over TCP, byte for byte as Redis 8.10.
TEST_F(LeanRedisTest, ZsetCompatibility) {
  start();
  size_t passed = 0, total = 0;
  for (const CompatGroup& g : kCompatGroups) {
    TestClient c(port_);
    ASSERT_EQ(c.call({"FLUSHALL"}), "+OK\r\n");
    for (const CompatStep& s : g.steps) {
      ++total;
      std::string got = c.call(s.args);
      bool same = s.order == kExact ? got == s.reply
                                    : testing::canonical(got, s.order) == testing::canonical(s.reply, s.order);
      if (!same) {
        ADD_FAILURE() << "[" << g.name << "] " << testing::show_args(s.args)
                      << "\n  expected: " << ::testing::PrintToString(s.reply)
                      << "\n  got:      " << ::testing::PrintToString(got);
        break;
      }
      ++passed;
    }
  }
  std::printf("zset compatibility: %zu of %zu steps match Redis\n", passed, total);
}

TEST_F(LeanRedisTest, Config) {
  start();
  TestClient c(port_);
  EXPECT_EQ(c.call({"CONFIG", "SET", "maxmemory", "123456"}), "+OK\r\n");
  EXPECT_EQ(c.call({"CONFIG", "GET", "maxmemory"}), "*2\r\n$9\r\nmaxmemory\r\n$6\r\n123456\r\n");
  EXPECT_EQ(c.call({"CONFIG", "SET", "maxmemory-policy", "volatile-ttl"}), "+OK\r\n");
  EXPECT_EQ(c.call({"CONFIG", "GET", "maxmemory-policy"}), "*2\r\n$16\r\nmaxmemory-policy\r\n$12\r\nvolatile-ttl\r\n");
  EXPECT_EQ(c.call({"CONFIG", "SET", "maxmemory", "0"}), "+OK\r\n");
}

// The cache scenario: 128 MB written into a 32 MB budget. No write fails; old keys make
// room; memory stays near the limit.
TEST_F(LeanRedisTest, EvictsToStayUnderMaxmemory) {
#if defined(__has_feature)
#if __has_feature(address_sanitizer)
  GTEST_SKIP() << "AddressSanitizer replaces malloc and quarantines freed memory: heap sizes mean nothing";
#endif
#endif
  LeanRedisOptions options;
  size_t budget = 32 << 20;
  options.eviction = {heap_bytes_in_use() + budget, EvictionPolicy::kAllKeysLru, 5};
  start(options);
  TestClient c(port_, 10000);
  std::string value(64 << 10, 'v');
  for (int i = 0; i < 2000; ++i) {
    std::string r = c.call({"SET", "k" + std::to_string(i), value});
    ASSERT_EQ(r, "+OK\r\n") << "write " << i;
  }
  std::string dbsize = c.call({"DBSIZE"});
  long n = std::stol(dbsize.substr(1));
  EXPECT_LT(n, 1000) << "nothing was evicted?";
  EXPECT_GT(n, 100) << "evicted far too much";
  EXPECT_EQ(c.call({"EXISTS", "k1999"}), ":1\r\n") << "the newest key was evicted";
  EXPECT_LT(heap_bytes_in_use(), options.eviction.maxmemory + (16 << 20));
}

TEST_F(LeanRedisTest, NoevictionRefusesWritesButServesReads) {
  LeanRedisOptions options;
  options.eviction = {heap_bytes_in_use() + (8 << 20), EvictionPolicy::kNoEviction, 5};
  start(options);
  TestClient c(port_, 10000);
  std::string value(64 << 10, 'v');
  std::string r;
  int i = 0;
  for (; i < 1000; ++i) {
    r = c.call({"SET", "k" + std::to_string(i), value});
    if (r != "+OK\r\n") break;
  }
  EXPECT_EQ(r, "-OOM command not allowed when used memory > 'maxmemory'.\r\n") << "after " << i << " writes";
  EXPECT_EQ(c.call({"EXISTS", "k0"}), ":1\r\n");
  EXPECT_EQ(c.call({"DEL", "k0"}), ":1\r\n") << "deletes still run";
}

// A leaderboard: many clients update scores; the top of the board is right.
TEST_F(LeanRedisTest, Leaderboard) {
  start();
  std::vector<std::thread> players;
  for (int t = 0; t < 8; ++t) {
    players.emplace_back([&, t] {
      TestClient c(port_);
      for (int i = 0; i < 250; ++i) c.send({"ZINCRBY", "board", "1", "player" + std::to_string(t)});
      for (int i = 0; i < 250; ++i) c.read_reply();
      c.call({"ZINCRBY", "board", std::to_string(t), "player" + std::to_string(t)});
    });
  }
  for (auto& p : players) p.join();
  TestClient c(port_);
  EXPECT_EQ(c.call({"ZRANGE", "board", "0", "1", "REV", "WITHSCORES"}),
            "*4\r\n$7\r\nplayer7\r\n$3\r\n257\r\n$7\r\nplayer6\r\n$3\r\n256\r\n");
  EXPECT_EQ(c.call({"ZRANK", "board", "player0"}), ":0\r\n");
}

}  // namespace
}  // namespace rd
