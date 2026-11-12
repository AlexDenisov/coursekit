#include "db/generic/generic.h"

#include <gtest/gtest.h>

#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include "common/transcript.h"
#include "db/keyspace/fake_list.h"

// Transcripts recorded from Redis 8.10: `python3 tools/redis_reference.py
// tools/cases/generic.txt`.

namespace rd {
namespace {

using testing::kAnyOrder;
using testing::play;

class Generic : public ::testing::Test {
 protected:
  Generic() {
    register_keyspace_commands(table, ks);
    register_generic_commands(table, ks, rng);
    testing::register_fake_rpush(table, ks);
  }
  std::string run(std::vector<std::string> args) {
    std::string reply;
    table.dispatch(args, reply);
    return reply;
  }
  // SCAN's reply: {next cursor, keys}.
  std::pair<std::string, std::vector<std::string>> scan(std::vector<std::string> args) {
    std::string r = run(args);
    EXPECT_EQ(r.rfind("*2\r\n$", 0), 0u) << r;
    size_t e = r.find("\r\n", 4);
    size_t len = std::stoul(r.substr(5, e - 5));
    std::string cursor = r.substr(e + 2, len);
    std::vector<std::string> keys;
    testing::split_array(r.substr(e + 2 + len + 2), &keys);
    return {cursor, keys};
  }
  FakeClock clock;
  Keyspace ks{clock};
  Rng rng{1};
  CommandTable table;
};

TEST_F(Generic, Keys) {
  play(table, {
      {{"MSET", "user:1", "a", "user:2", "b", "user:10", "c", "other", "d"}, "+OK\r\n"},
      {{"KEYS", "user:?"}, "*2\r\n$6\r\nuser:1\r\n$6\r\nuser:2\r\n", kAnyOrder},
      {{"KEYS", "user:*"}, "*3\r\n$6\r\nuser:1\r\n$7\r\nuser:10\r\n$6\r\nuser:2\r\n", kAnyOrder},
      {{"KEYS", "*"}, "*4\r\n$5\r\nother\r\n$6\r\nuser:1\r\n$7\r\nuser:10\r\n$6\r\nuser:2\r\n", kAnyOrder},
      {{"KEYS", "nomatch*"}, "*0\r\n"},
      {{"DBSIZE"}, ":4\r\n"},
  });
}

// "*" is a fast path in Redis: no matching, so even the empty key is listed (the matcher
// itself never matches an empty string; see db/glob).
TEST_F(Generic, KeysStarListsTheEmptyKey) {
  play(table, {
      {{"SET", "", "empty"}, "+OK\r\n"},
      {{"KEYS", "*"}, "*1\r\n$0\r\n\r\n"},
  });
}

TEST_F(Generic, KeysSkipsExpiredKeys) {
  play(table, {{{"MSET", "a", "1", "b", "2"}, "+OK\r\n"}});
  ks.set_expire("a", clock.now_ms() + 10);
  clock.advance(10);
  play(table, {{{"KEYS", "*"}, "*1\r\n$1\r\nb\r\n"}});
}

TEST_F(Generic, Rename) {
  play(table, {
      {{"SET", "a", "1"}, "+OK\r\n"},
      {{"RENAME", "a", "b"}, "+OK\r\n"},
      {{"GET", "a"}, "$-1\r\n"},
      {{"GET", "b"}, "$1\r\n1\r\n"},
      {{"RENAME", "missing", "x"}, "-ERR no such key\r\n"},
      {{"RENAME", "b", "b"}, "+OK\r\n"},
      {{"GET", "b"}, "$1\r\n1\r\n"},
      {{"SET", "c", "3"}, "+OK\r\n"},
      {{"RENAME", "b", "c"}, "+OK\r\n"},
      {{"GET", "c"}, "$1\r\n1\r\n"},
      {{"RENAMENX", "c", "d"}, ":1\r\n"},
      {{"RENAMENX", "c", "d"}, "-ERR no such key\r\n"},
      {{"SET", "e", "5"}, "+OK\r\n"},
      {{"RENAMENX", "d", "e"}, ":0\r\n"},
      {{"RENAMENX", "missing", "x"}, "-ERR no such key\r\n"},
  });
}

// Redis: RENAME moves the value as it is, whatever its type, and its TTL with it.
TEST_F(Generic, RenameKeepsTypeAndTtl) {
  play(table, {{{"RPUSH", "l", "x"}, ":1\r\n"}});
  ks.set_expire("l", clock.now_ms() + 100'000);
  play(table, {{{"RENAME", "l", "m"}, "+OK\r\n"}, {{"TYPE", "m"}, "+list\r\n"}, {{"TYPE", "l"}, "+none\r\n"}});
  EXPECT_EQ(ks.get_expire("m"), clock.now_ms() + 100'000);
  EXPECT_EQ(ks.get_expire("l"), std::nullopt);
}

TEST_F(Generic, RenameDropsTheTargetsOldTtl) {
  play(table, {{{"SET", "a", "1"}, "+OK\r\n"}, {{"SET", "b", "2"}, "+OK\r\n"}});
  ks.set_expire("b", clock.now_ms() + 5000);
  play(table, {{{"RENAME", "a", "b"}, "+OK\r\n"}});
  EXPECT_EQ(ks.get_expire("b"), std::nullopt);
}

TEST_F(Generic, FlushAndDbsize) {
  play(table, {
      {{"MSET", "a", "1", "b", "2", "c", "3"}, "+OK\r\n"},
      {{"DBSIZE"}, ":3\r\n"},
      {{"FLUSHDB"}, "+OK\r\n"},
      {{"DBSIZE"}, ":0\r\n"},
      {{"MSET", "a", "1"}, "+OK\r\n"},
      {{"FLUSHALL"}, "+OK\r\n"},
      {{"DBSIZE"}, ":0\r\n"},
      {{"FLUSHDB", "ASYNC"}, "+OK\r\n"},
      {{"FLUSHDB", "FOO"}, "-ERR syntax error\r\n"},
  });
}

TEST_F(Generic, Randomkey) {
  play(table, {
      {{"RANDOMKEY"}, "$-1\r\n"},
      {{"SET", "only", "1"}, "+OK\r\n"},
      {{"RANDOMKEY"}, "$4\r\nonly\r\n"},
  });
}

TEST_F(Generic, RandomkeySkipsExpiredKeys) {
  for (int i = 0; i < 50; ++i) {
    play(table, {{{"SET", "dead" + std::to_string(i), "1", "PX", "10"}, "+OK\r\n"}});
  }
  play(table, {{{"SET", "alive", "1"}, "+OK\r\n"}});
  clock.advance(10);
  for (int i = 0; i < 20; ++i) play(table, {{{"RANDOMKEY"}, "$5\r\nalive\r\n"}});
}

TEST_F(Generic, ScanErrors) {
  play(table, {
      {{"SCAN", "abc"}, "-ERR invalid cursor\r\n"},
      {{"SCAN", "0", "COUNT", "0"}, "-ERR syntax error\r\n"},
      {{"SCAN", "0", "COUNT", "abc"}, "-ERR value is not an integer or out of range\r\n"},
      {{"SCAN", "0", "FOO"}, "-ERR syntax error\r\n"},
      {{"SCAN", "0", "MATCH"}, "-ERR syntax error\r\n"},
      {{"SCAN", "0", "TYPE", "nosuchtype"}, "*2\r\n$1\r\n0\r\n*0\r\n"},
      {{"SCAN"}, "-ERR wrong number of arguments for 'scan' command\r\n"},
  });
}

TEST_F(Generic, ScanVisitsEverything) {
  for (int i = 0; i < 500; ++i) run({"SET", "k" + std::to_string(i), "v"});
  std::map<std::string, int> seen;
  std::string cursor = "0";
  int calls = 0;
  do {
    auto [next, keys] = scan({"SCAN", cursor});
    for (auto& k : keys) ++seen[k];
    cursor = next;
    ASSERT_LT(++calls, 10000);
  } while (cursor != "0");
  EXPECT_EQ(seen.size(), 500u);
  EXPECT_GT(calls, 10) << "COUNT 10 should take many calls for 500 keys";
}

TEST_F(Generic, ScanWithABigCountFinishesInOneCall) {
  for (int i = 0; i < 100; ++i) run({"SET", "k" + std::to_string(i), "v"});
  auto [cursor, keys] = scan({"SCAN", "0", "COUNT", "1000"});
  EXPECT_EQ(cursor, "0");
  EXPECT_EQ(keys.size(), 100u);
}

TEST_F(Generic, ScanMatchAndType) {
  for (int i = 0; i < 50; ++i) {
    run({"SET", "user:" + std::to_string(i), "v"});
    run({"SET", "item:" + std::to_string(i), "v"});
  }
  run({"RPUSH", "user:list", "x"});
  std::set<std::string> users, lists;
  std::string c1 = "0", c2 = "0";
  do {
    auto [next, keys] = scan({"SCAN", c1, "MATCH", "user:*", "COUNT", "7"});
    users.insert(keys.begin(), keys.end());
    c1 = next;
  } while (c1 != "0");
  do {
    auto [next, keys] = scan({"SCAN", c2, "TYPE", "list"});
    lists.insert(keys.begin(), keys.end());
    c2 = next;
  } while (c2 != "0");
  EXPECT_EQ(users.size(), 51u);
  for (auto& k : users) EXPECT_EQ(k.rfind("user:", 0), 0u) << k;
  EXPECT_EQ(lists, std::set<std::string>{"user:list"});
}

// The SCAN guarantee, through the command: keys written between calls make the table
// grow, and every original key still comes back.
TEST_F(Generic, ScanSurvivesWritesBetweenCalls) {
  for (int i = 0; i < 200; ++i) run({"SET", "orig" + std::to_string(i), "v"});
  std::set<std::string> seen;
  std::string cursor = "0";
  int extra = 0;
  do {
    auto [next, keys] = scan({"SCAN", cursor, "COUNT", "5"});
    seen.insert(keys.begin(), keys.end());
    cursor = next;
    for (int j = 0; j < 5 && extra < 3000; ++j) run({"SET", "new" + std::to_string(extra++), "v"});
  } while (cursor != "0");
  for (int i = 0; i < 200; ++i) EXPECT_TRUE(seen.count("orig" + std::to_string(i))) << i;
}

TEST_F(Generic, ScanSkipsExpiredKeys) {
  run({"SET", "dead", "1", "PX", "10"});
  run({"SET", "alive", "1"});
  clock.advance(10);
  auto [cursor, keys] = scan({"SCAN", "0", "COUNT", "100"});
  EXPECT_EQ(keys, std::vector<std::string>{"alive"});
}

}  // namespace
}  // namespace rd
