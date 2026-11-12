#include "server/mini_redis/mini_redis.h"

#include <gtest/gtest.h>

#include <chrono>
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

struct CompatStep {
  std::vector<std::string> args;
  std::string reply;
  Order order;
};
struct CompatGroup {
  std::string name;
  std::vector<CompatStep> steps;
};

using namespace std::string_literals;  // the table uses "..."s for strings with NUL bytes

// Every case file recorded so far, replayed against real Redis:
// `python3 tools/redis_reference.py --inc tools/cases/{dispatch,strings,keyspace,expire,
// lists,hashes,sets,generic}.txt > server/mini_redis/compat_cases.inc`
#include "server/mini_redis/compat_cases.inc"

class MiniRedisTest : public ::testing::Test {
 protected:
  void SetUp() override {
    server_ = std::make_unique<MiniRedis>(clock_);
    port_ = server_->listen("127.0.0.1", 0);
    ASSERT_NE(port_, 0);
    thread_ = std::thread([this] { server_->run(); });
  }
  void TearDown() override {
    server_->stop();
    if (thread_.joinable()) thread_.join();
  }
  SystemClock clock_;
  std::unique_ptr<MiniRedis> server_;
  uint16_t port_ = 0;
  std::thread thread_;
};

// The milestone: the same commands, over TCP, give the same bytes as Redis 8.10.
TEST_F(MiniRedisTest, CompatibilitySuite) {
  size_t passed = 0, total = 0;
  for (const CompatGroup& g : kCompatGroups) {
    TestClient c(port_);
    if (c.call({"FLUSHALL"}) != "+OK\r\n") {
      ADD_FAILURE() << "FLUSHALL failed; is db/generic registered?";
      return;
    }
    bool ok = true;
    for (const CompatStep& s : g.steps) {
      ++total;
      if (!ok) continue;  // later steps depend on earlier ones
      std::string got = c.call(s.args);
      bool same = s.order == kExact ? got == s.reply
                                    : testing::canonical(got, s.order) == testing::canonical(s.reply, s.order);
      if (same) {
        ++passed;
        continue;
      }
      ok = false;
      ADD_FAILURE() << "[" << g.name << "] " << testing::show_args(s.args)
                    << "\n  expected: " << ::testing::PrintToString(s.reply)
                    << "\n  got:      " << ::testing::PrintToString(got);
      if (!c.connected()) return;
    }
  }
  std::printf("compatibility: %zu of %zu steps match Redis\n", passed, total);
}

// Active expiry: nobody reads these keys again, yet the cron deletes them. DBSIZE counts
// stored keys, expired or not, so it only drops if the cron works.
TEST_F(MiniRedisTest, CronExpiresKeysNobodyReads) {
  TestClient c(port_);
  for (int i = 0; i < 200; ++i) c.send({"SET", "k" + std::to_string(i), "v", "PX", "50"});
  for (int i = 0; i < 200; ++i) ASSERT_EQ(c.read_reply(), "+OK\r\n");
  EXPECT_EQ(c.call({"DBSIZE"}), ":200\r\n");
  std::string size;
  for (int i = 0; i < 50; ++i) {
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    size = c.call({"DBSIZE"});
    if (size == ":0\r\n") break;
  }
  EXPECT_EQ(size, ":0\r\n") << "expired keys linger: is the cron running active_expire_cycle?";
}

TEST_F(MiniRedisTest, ManyClientsManyTypes) {
  std::vector<std::thread> threads;
  for (int t = 0; t < 8; ++t) {
    threads.emplace_back([&, t] {
      TestClient c(port_);
      std::string id = std::to_string(t);
      for (int i = 0; i < 100; ++i) {
        c.send({"RPUSH", "list:" + id, std::to_string(i)});
        c.send({"HSET", "hash:" + id, "f" + std::to_string(i), "v"});
        c.send({"SADD", "set", id + ":" + std::to_string(i)});
        c.send({"INCR", "counter"});
      }
      for (int i = 0; i < 400; ++i) c.read_reply();
    });
  }
  for (auto& t : threads) t.join();
  TestClient c(port_);
  EXPECT_EQ(c.call({"GET", "counter"}), "$3\r\n800\r\n");
  EXPECT_EQ(c.call({"SCARD", "set"}), ":800\r\n");
  EXPECT_EQ(c.call({"LLEN", "list:3"}), ":100\r\n");
  EXPECT_EQ(c.call({"HLEN", "hash:7"}), ":100\r\n");
  EXPECT_EQ(c.call({"LINDEX", "list:5", "-1"}), "$2\r\n99\r\n");
}

// cron() also advances rehashing, so a big keyspace doesn't stay half-rehashed forever
// when there's no traffic.
TEST(MiniRedisCron, FinishesRehashingWithoutTraffic) {
  FakeClock clock;
  MiniRedis server(clock);
  Keyspace& ks = server.keyspace();
  int i = 0;
  while ((!ks.entries().is_rehashing() || ks.size() < 1000) && i < 100000) {
    ks.set("k" + std::to_string(i++), std::make_unique<StringValue>("v"));
  }
  ASSERT_TRUE(ks.entries().is_rehashing());
  for (int n = 0; n < 1000 && ks.entries().is_rehashing(); ++n) server.cron();
  EXPECT_FALSE(ks.entries().is_rehashing());
}

}  // namespace
}  // namespace rd
