#include "db/expire/expire.h"

#include <gtest/gtest.h>

#include <memory>
#include <string>

#include "common/transcript.h"

// Transcripts recorded from Redis 8.10: `python3 tools/redis_reference.py
// tools/cases/expire.txt`. Redis's real clock barely moves during a recording, so TTL
// replies there match a frozen fake clock.

namespace rd {
namespace {

using testing::play;

class Expire : public ::testing::Test {
 protected:
  Expire() {
    register_keyspace_commands(table, ks);
    register_expire_commands(table, ks);
  }
  FakeClock clock{1'700'000'000'000};
  Keyspace ks{clock};
  CommandTable table;
};

TEST_F(Expire, Basics) {
  play(table, {
      {{"SET", "k", "v"}, "+OK\r\n"},
      {{"TTL", "k"}, ":-1\r\n"},
      {{"PTTL", "k"}, ":-1\r\n"},
      {{"TTL", "missing"}, ":-2\r\n"},
      {{"PTTL", "missing"}, ":-2\r\n"},
      {{"EXPIRE", "k", "100"}, ":1\r\n"},
      {{"TTL", "k"}, ":100\r\n"},
      {{"PTTL", "k"}, ":100000\r\n"},
      {{"EXPIRE", "missing", "100"}, ":0\r\n"},
      {{"PERSIST", "k"}, ":1\r\n"},
      {{"TTL", "k"}, ":-1\r\n"},
      {{"PERSIST", "k"}, ":0\r\n"},
      {{"PERSIST", "missing"}, ":0\r\n"},
      {{"EXPIRETIME", "k"}, ":-1\r\n"},
      {{"EXPIRETIME", "missing"}, ":-2\r\n"},
  });
}

TEST_F(Expire, Errors) {
  play(table, {
      {{"SET", "k", "v"}, "+OK\r\n"},
      {{"EXPIRE", "k", "abc"}, "-ERR value is not an integer or out of range\r\n"},
      {{"EXPIRE", "k", "1.5"}, "-ERR value is not an integer or out of range\r\n"},
      {{"EXPIRE", "k", "9223372036854775807"}, "-ERR invalid expire time in 'expire' command\r\n"},
      {{"PEXPIRE", "k", "9223372036854775807"}, "-ERR invalid expire time in 'pexpire' command\r\n"},
      {{"EXPIRE", "k", "100", "FOO"}, "-ERR Unsupported option FOO\r\n"},
      {{"EXPIRE", "k", "100", "NX", "XX"},
       "-ERR NX and XX, GT or LT options at the same time are not compatible\r\n"},
      {{"EXPIRE", "k", "100", "GT", "LT"}, "-ERR GT and LT options at the same time are not compatible\r\n"},
      {{"EXPIRE", "k", "100", "NX", "GT"},
       "-ERR NX and XX, GT or LT options at the same time are not compatible\r\n"},
      {{"EXPIRE"}, "-ERR wrong number of arguments for 'expire' command\r\n"},
      {{"EXPIRE", "k"}, "-ERR wrong number of arguments for 'expire' command\r\n"},
      {{"TTL", "k"}, ":-1\r\n"},
  });
}

TEST_F(Expire, TimesInThePastDelete) {
  play(table, {
      {{"SET", "k", "v"}, "+OK\r\n"},
      {{"EXPIRE", "k", "0"}, ":1\r\n"},
      {{"EXISTS", "k"}, ":0\r\n"},
      {{"SET", "k", "v"}, "+OK\r\n"},
      {{"EXPIRE", "k", "-10"}, ":1\r\n"},
      {{"EXISTS", "k"}, ":0\r\n"},
      {{"SET", "k", "v"}, "+OK\r\n"},
      {{"PEXPIREAT", "k", "1"}, ":1\r\n"},
      {{"EXISTS", "k"}, ":0\r\n"},
      {{"SET", "k", "v"}, "+OK\r\n"},
      {{"EXPIREAT", "k", "1"}, ":1\r\n"},
      {{"EXISTS", "k"}, ":0\r\n"},
      {{"EXPIRE", "missing", "-1"}, ":0\r\n"},
  });
}

TEST_F(Expire, Conditions) {
  play(table, {
      {{"SET", "k", "v"}, "+OK\r\n"},
      {{"EXPIRE", "k", "100", "XX"}, ":0\r\n"},
      {{"EXPIRE", "k", "100", "GT"}, ":0\r\n"},
      {{"EXPIRE", "k", "100", "LT"}, ":1\r\n"},
      {{"TTL", "k"}, ":100\r\n"},
      {{"EXPIRE", "k", "200", "NX"}, ":0\r\n"},
      {{"EXPIRE", "k", "200", "XX"}, ":1\r\n"},
      {{"TTL", "k"}, ":200\r\n"},
      {{"EXPIRE", "k", "50", "GT"}, ":0\r\n"},
      {{"EXPIRE", "k", "300", "GT"}, ":1\r\n"},
      {{"TTL", "k"}, ":300\r\n"},
      {{"EXPIRE", "k", "400", "LT"}, ":0\r\n"},
      {{"EXPIRE", "k", "10", "LT"}, ":1\r\n"},
      {{"TTL", "k"}, ":10\r\n"},
      {{"EXPIRE", "k", "20", "nx"}, ":0\r\n"},
      {{"EXPIRE", "k", "20", "xx", "gt"}, ":1\r\n"},
      {{"TTL", "k"}, ":20\r\n"},
      {{"PERSIST", "k"}, ":1\r\n"},
      {{"EXPIRE", "k", "30", "gt"}, ":0\r\n"},
      {{"TTL", "k"}, ":-1\r\n"},
  });
}

// TTL rounds to the nearest second; PTTL is exact.
TEST_F(Expire, TtlRounding) {
  play(table, {{{"SET", "k", "v"}, "+OK\r\n"}, {{"PEXPIRE", "k", "1499"}, ":1\r\n"}});
  play(table, {{{"TTL", "k"}, ":1\r\n"}, {{"PTTL", "k"}, ":1499\r\n"}});
  play(table, {{{"PEXPIRE", "k", "1500"}, ":1\r\n"}, {{"TTL", "k"}, ":2\r\n"}});
  clock.advance(1400);
  play(table, {{{"TTL", "k"}, ":0\r\n"}, {{"PTTL", "k"}, ":100\r\n"}});
  clock.advance(100);
  play(table, {{{"TTL", "k"}, ":-2\r\n"}});
}

TEST_F(Expire, AbsoluteTimes) {
  int64_t at = 1'800'000'000;  // seconds, in the future of the fake clock
  play(table, {
      {{"SET", "k", "v"}, "+OK\r\n"},
      {{"EXPIREAT", "k", std::to_string(at)}, ":1\r\n"},
      {{"EXPIRETIME", "k"}, ":" + std::to_string(at) + "\r\n"},
      {{"PEXPIRETIME", "k"}, ":" + std::to_string(at * 1000) + "\r\n"},
      {{"PEXPIREAT", "k", std::to_string(at * 1000 + 250)}, ":1\r\n"},
      {{"PEXPIRETIME", "k"}, ":" + std::to_string(at * 1000 + 250) + "\r\n"},
  });
}

TEST_F(Expire, ExpiredKeysAreGoneForTtl) {
  play(table, {{{"SET", "k", "v", "PX", "10"}, "+OK\r\n"}});
  clock.advance(10);
  play(table, {{{"TTL", "k"}, ":-2\r\n"}, {{"PERSIST", "k"}, ":0\r\n"}});
}

// ---- active expiry ----

class ActiveExpire : public ::testing::Test {
 protected:
  void add(int n, int64_t ttl_ms, const std::string& prefix) {
    for (int i = 0; i < n; ++i) {
      std::string key = prefix + std::to_string(i);
      ks.set(key, std::make_unique<StringValue>("v"));
      ks.set_expire(key, clock.now_ms() + ttl_ms);
    }
  }
  FakeClock clock{1'700'000'000'000};
  Keyspace ks{clock};
  Rng rng{2024};
};

TEST_F(ActiveExpire, NoTtlsNoWork) {
  ks.set("plain", std::make_unique<StringValue>("v"));
  ExpireCycleStats s = active_expire_cycle(ks, rng);
  EXPECT_EQ(s.loops, 0u);
  EXPECT_EQ(s.sampled, 0u);
}

TEST_F(ActiveExpire, NothingExpiredMeansOneRound) {
  add(1000, 60'000, "live");
  ExpireCycleStats s = active_expire_cycle(ks, rng);
  EXPECT_EQ(s.loops, 1u);
  EXPECT_EQ(s.sampled, 20u);
  EXPECT_EQ(s.expired, 0u);
  EXPECT_EQ(ks.size(), 1000u);
}

// Every sample is 100% stale, so the cycle keeps going until max_loops.
TEST_F(ActiveExpire, AllExpiredRunsEveryRound) {
  add(1000, 100, "dead");
  clock.advance(100);
  ExpireCycleStats s = active_expire_cycle(ks, rng, 16);
  EXPECT_EQ(s.loops, 16u);
  EXPECT_EQ(s.sampled, 320u);
  EXPECT_EQ(s.expired, 320u);
  EXPECT_EQ(ks.size(), 680u);
}

TEST_F(ActiveExpire, FewerKeysThanASample) {
  add(5, 100, "dead");
  clock.advance(100);
  ExpireCycleStats s = active_expire_cycle(ks, rng, 16);
  EXPECT_EQ(s.expired, 5u);
  EXPECT_EQ(ks.size(), 0u);
  EXPECT_EQ(ks.expires().size(), 0u);
}

// The invariant that matters: keys that haven't expired are never touched, however many
// rounds run.
TEST_F(ActiveExpire, NeverDeletesLiveKeys) {
  add(500, 100, "dead");
  add(500, 60'000, "live");
  ks.set("plain", std::make_unique<StringValue>("v"));
  clock.advance(100);
  for (int i = 0; i < 2000; ++i) active_expire_cycle(ks, rng, 16);
  EXPECT_EQ(ks.size(), 501u) << "all dead keys gone, all live ones (and the plain one) kept";
  for (int i = 0; i < 500; ++i) EXPECT_TRUE(ks.entries().find("live" + std::to_string(i)));
}

TEST_F(ActiveExpire, DeletionsAreNotCommandWrites) {
  add(100, 100, "dead");
  clock.advance(100);
  uint64_t before = ks.dirty();
  active_expire_cycle(ks, rng, 16);
  EXPECT_EQ(ks.dirty(), before);
}

}  // namespace
}  // namespace rd
