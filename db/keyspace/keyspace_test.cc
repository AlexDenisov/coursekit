#include "db/keyspace/keyspace.h"

#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <vector>

#include "common/transcript.h"
#include "db/keyspace/fake_list.h"

namespace rd {
namespace {

using testing::play;

std::unique_ptr<Value> str(std::string s) { return std::make_unique<StringValue>(std::move(s)); }

std::string get_str(Keyspace& ks, std::string_view key) {
  Value* v = ks.find(key);
  return v ? static_cast<StringValue*>(v)->s : "<missing>";
}

// ---- StringValue::encoding (rules recorded from Redis's OBJECT ENCODING) ----

TEST(StringEncoding, IntEmbstrRaw) {
  EXPECT_EQ(StringValue("123").encoding(), "int");
  EXPECT_EQ(StringValue("-123").encoding(), "int");
  EXPECT_EQ(StringValue("9223372036854775807").encoding(), "int");
  EXPECT_EQ(StringValue("9223372036854775808").encoding(), "embstr");
  EXPECT_EQ(StringValue("01").encoding(), "embstr");
  EXPECT_EQ(StringValue("hello").encoding(), "embstr");
  EXPECT_EQ(StringValue(std::string(44, 'x')).encoding(), "embstr");
  EXPECT_EQ(StringValue(std::string(45, 'x')).encoding(), "raw");
  EXPECT_EQ(StringValue("hello", /*raw=*/true).encoding(), "raw");
}

// ---- the Keyspace API ----

class KeyspaceApi : public ::testing::Test {
 protected:
  FakeClock clock_{1'000'000};
  Keyspace ks_{clock_};
};

TEST_F(KeyspaceApi, SetFindErase) {
  EXPECT_EQ(ks_.find("k"), nullptr);
  ks_.set("k", str("v"));
  EXPECT_EQ(get_str(ks_, "k"), "v");
  EXPECT_EQ(ks_.size(), 1u);
  ks_.set("k", str("w"));
  EXPECT_EQ(get_str(ks_, "k"), "w");
  EXPECT_TRUE(ks_.erase("k"));
  EXPECT_FALSE(ks_.erase("k"));
  EXPECT_EQ(ks_.size(), 0u);
}

// Lazy expiry: an expired key disappears the moment anyone looks at it.
TEST_F(KeyspaceApi, LazyExpiry) {
  ks_.set("k", str("v"));
  ASSERT_TRUE(ks_.set_expire("k", clock_.now_ms() + 100));
  EXPECT_EQ(ks_.get_expire("k"), clock_.now_ms() + 100);
  clock_.advance(99);
  EXPECT_EQ(get_str(ks_, "k"), "v");
  clock_.advance(1);
  EXPECT_TRUE(ks_.is_expired("k"));
  EXPECT_EQ(ks_.size(), 1u) << "nobody has looked yet";
  EXPECT_EQ(ks_.find("k"), nullptr);
  EXPECT_EQ(ks_.size(), 0u) << "looking at an expired key deletes it";
  EXPECT_EQ(ks_.get_expire("k"), std::nullopt) << "...and its TTL";
}

TEST_F(KeyspaceApi, ExpiredKeyIsMissingForEveryAccess) {
  for (int which = 0; which < 3; ++which) {
    ks_.set("k", str("v"));
    ks_.set_expire("k", clock_.now_ms() - 1);
    if (which == 0) EXPECT_EQ(ks_.find_for_write("k"), nullptr);
    if (which == 1) EXPECT_FALSE(ks_.erase("k"));
    if (which == 2) EXPECT_EQ(ks_.find("k"), nullptr);
    EXPECT_EQ(ks_.size(), 0u);
  }
}

TEST_F(KeyspaceApi, SetExpireNeedsAKey) {
  EXPECT_FALSE(ks_.set_expire("nope", clock_.now_ms() + 10));
  EXPECT_EQ(ks_.get_expire("nope"), std::nullopt);
}

TEST_F(KeyspaceApi, SetDropsTheTtlUnlessKeepTtl) {
  ks_.set("k", str("v"));
  ks_.set_expire("k", clock_.now_ms() + 100);
  ks_.set("k", str("w"), /*keep_ttl=*/true);
  EXPECT_EQ(ks_.get_expire("k"), clock_.now_ms() + 100);
  ks_.set("k", str("x"));
  EXPECT_EQ(ks_.get_expire("k"), std::nullopt);
}

TEST_F(KeyspaceApi, Persist) {
  ks_.set("k", str("v"));
  EXPECT_FALSE(ks_.persist("k"));
  ks_.set_expire("k", clock_.now_ms() + 100);
  EXPECT_TRUE(ks_.persist("k"));
  EXPECT_EQ(ks_.get_expire("k"), std::nullopt);
}

TEST_F(KeyspaceApi, EraseDropsTheTtl) {
  ks_.set("k", str("v"));
  ks_.set_expire("k", clock_.now_ms() + 100);
  ks_.erase("k");
  ks_.set("k", str("v"));
  EXPECT_EQ(ks_.get_expire("k"), std::nullopt);
}

TEST_F(KeyspaceApi, DirtyCountsWrites) {
  uint64_t d0 = ks_.dirty();
  ks_.set("k", str("v"));
  EXPECT_EQ(ks_.dirty(), d0 + 1);
  ks_.find("k");
  ks_.find("missing");
  ks_.find_for_write("missing");
  EXPECT_EQ(ks_.dirty(), d0 + 1) << "reads and misses aren't writes";
  ks_.find_for_write("k");
  EXPECT_EQ(ks_.dirty(), d0 + 2);
  ks_.erase("missing");
  EXPECT_EQ(ks_.dirty(), d0 + 2);
  ks_.erase("k");
  EXPECT_EQ(ks_.dirty(), d0 + 3);
  ks_.set("e", str("v"));
  ks_.set_expire("e", clock_.now_ms() - 1);
  uint64_t before = ks_.dirty();
  ks_.find("e");
  EXPECT_EQ(ks_.dirty(), before) << "lazy expiry isn't a write by a command";
}

TEST_F(KeyspaceApi, OnModifiedHook) {
  std::vector<std::string> touched;
  ks_.set_on_modified([&](std::string_view k) { touched.emplace_back(k); });
  ks_.set("a", str("1"));
  ks_.find("a");
  ks_.find_for_write("a");
  ks_.erase("a");
  ks_.erase("a");
  EXPECT_EQ(touched, (std::vector<std::string>{"a", "a", "a"}));
}

TEST_F(KeyspaceApi, ClearAndFlushHook) {
  int flushes = 0;
  ks_.set_on_flush([&] { ++flushes; });
  for (int i = 0; i < 10; ++i) {
    ks_.set("k" + std::to_string(i), str("v"));
    ks_.set_expire("k" + std::to_string(i), clock_.now_ms() + 1000);
  }
  ks_.clear();
  EXPECT_EQ(ks_.size(), 0u);
  EXPECT_EQ(ks_.expires().size(), 0u);
  EXPECT_EQ(flushes, 1);
}

TEST_F(KeyspaceApi, AccessUpdatesTheLruClock) {
  ks_.set("k", str("v"));
  EXPECT_EQ(ks_.entries().find("k")->lru, lru_clock(clock_.now_ms()));
  clock_.advance(5000);
  ks_.find("k");
  EXPECT_EQ(ks_.entries().find("k")->lru, lru_clock(clock_.now_ms()));
}

class EmptyAggregate final : public Value {
 public:
  static constexpr Type kType = Type::kSet;
  Type type() const override { return kType; }
  std::string_view encoding() const override { return "hashtable"; }
  bool empty() const override { return true; }
  size_t approx_bytes() const override { return 0; }
};

TEST_F(KeyspaceApi, EraseIfEmpty) {
  ks_.set("s", std::make_unique<EmptyAggregate>());
  ks_.set("k", str(""));
  ks_.erase_if_empty("s");
  ks_.erase_if_empty("k");
  ks_.erase_if_empty("missing");
  EXPECT_EQ(ks_.find("s"), nullptr);
  EXPECT_NE(ks_.find("k"), nullptr) << "an empty string is a value, not an empty aggregate";
}

TEST_F(KeyspaceApi, TypedLookup) {
  ks_.set("s", str("v"));
  ks_.set("l", std::make_unique<testing::FakeList>());
  EXPECT_NE(lookup<StringValue>(ks_, "s", false).value, nullptr);
  EXPECT_TRUE(lookup<StringValue>(ks_, "l", false).wrong_type);
  auto missing = lookup<StringValue>(ks_, "m", false);
  EXPECT_EQ(missing.value, nullptr);
  EXPECT_FALSE(missing.wrong_type);
}

// ---- the commands (transcripts recorded from Redis: tools/cases/keyspace.txt and
// tools/cases/strings.txt) ----

class Commands : public ::testing::Test {
 protected:
  Commands() {
    register_connection_commands(table);
    register_keyspace_commands(table, ks);
    testing::register_fake_rpush(table, ks);
  }
  FakeClock clock{1'700'000'000'000};
  Keyspace ks{clock};
  CommandTable table;
};

TEST_F(Commands, Type) {
  play(table, {
      {{"SET", "s", "v"}, "+OK\r\n"},
      {{"RPUSH", "l", "a"}, ":1\r\n"},
      {{"TYPE", "s"}, "+string\r\n"},
      {{"TYPE", "l"}, "+list\r\n"},
      {{"TYPE", "missing"}, "+none\r\n"},
      {{"TYPE"}, "-ERR wrong number of arguments for 'type' command\r\n"},
  });
}

TEST_F(Commands, WrongType) {
  const std::string wrong = "-WRONGTYPE Operation against a key holding the wrong kind of value\r\n";
  play(table, {
      {{"RPUSH", "l", "a"}, ":1\r\n"},
      {{"GET", "l"}, wrong},
      {{"INCR", "l"}, wrong},
      {{"APPEND", "l", "x"}, wrong},
      {{"STRLEN", "l"}, wrong},
      {{"SET", "l", "v", "GET"}, wrong},
      {{"TYPE", "l"}, "+list\r\n"},
      {{"MGET", "l"}, "*1\r\n$-1\r\n"},
      {{"EXISTS", "l"}, ":1\r\n"},
      {{"SET", "l", "v"}, "+OK\r\n"},
      {{"TYPE", "l"}, "+string\r\n"},
      {{"GET", "l"}, "$1\r\nv\r\n"},
  });
}

TEST_F(Commands, DelWorksOnAnyType) {
  play(table, {
      {{"RPUSH", "l", "a"}, ":1\r\n"},
      {{"DEL", "l"}, ":1\r\n"},
      {{"EXISTS", "l"}, ":0\r\n"},
  });
}

TEST_F(Commands, ObjectEncoding) {
  play(table, {
      {{"SET", "a", "123"}, "+OK\r\n"},
      {{"OBJECT", "ENCODING", "a"}, "$3\r\nint\r\n"},
      {{"SET", "a", "01"}, "+OK\r\n"},
      {{"OBJECT", "ENCODING", "a"}, "$6\r\nembstr\r\n"},
      {{"SET", "a", "123456789012345678901234567890123456789012345"}, "+OK\r\n"},
      {{"OBJECT", "ENCODING", "a"}, "$3\r\nraw\r\n"},
      {{"SET", "a", "hello"}, "+OK\r\n"},
      {{"APPEND", "a", "x"}, ":6\r\n"},
      {{"OBJECT", "ENCODING", "a"}, "$3\r\nraw\r\n"},
      {{"SET", "n", "10"}, "+OK\r\n"},
      {{"INCR", "n"}, ":11\r\n"},
      {{"OBJECT", "ENCODING", "n"}, "$3\r\nint\r\n"},
      {{"SET", "n", "abc"}, "+OK\r\n"},
      {{"APPEND", "n", "1"}, ":4\r\n"},
      {{"SET", "n", "5"}, "+OK\r\n"},
      {{"OBJECT", "ENCODING", "n"}, "$3\r\nint\r\n"},
      {{"OBJECT", "ENCODING", "missing"}, "$-1\r\n"},
      {{"OBJECT", "ENCODING"}, "-ERR wrong number of arguments for 'object|encoding' command\r\n"},
      {{"OBJECT", "FOO", "a"}, "-ERR unknown subcommand 'FOO'. Try OBJECT HELP.\r\n"},
  });
}

TEST_F(Commands, SetExpireOptionErrors) {
  play(table, {
      {{"SET", "k", "v", "EX", "0"}, "-ERR invalid expire time in 'set' command\r\n"},
      {{"SET", "k", "v", "EX", "-1"}, "-ERR invalid expire time in 'set' command\r\n"},
      {{"SET", "k", "v", "EX", "abc"}, "-ERR value is not an integer or out of range\r\n"},
      {{"SET", "k", "v", "PX", "0"}, "-ERR invalid expire time in 'set' command\r\n"},
      {{"SET", "k", "v", "EX", "10", "PX", "10"}, "-ERR syntax error\r\n"},
      {{"SET", "k", "v", "EX", "10", "KEEPTTL"}, "-ERR syntax error\r\n"},
      {{"SET", "k", "v", "EX"}, "-ERR syntax error\r\n"},
      {{"SET", "k", "v", "EX", "9223372036854775807"}, "-ERR invalid expire time in 'set' command\r\n"},
      {{"SET", "k", "v", "EXAT", "0"}, "-ERR invalid expire time in 'set' command\r\n"},
      {{"EXISTS", "k"}, ":0\r\n"},
  });
}

TEST_F(Commands, SetWithTtls) {
  int64_t now = clock.now_ms();
  play(table, {{{"SET", "k", "v", "ex", "100"}, "+OK\r\n"}});
  EXPECT_EQ(ks.get_expire("k"), now + 100'000);
  play(table, {{{"SET", "k", "v2", "KEEPTTL"}, "+OK\r\n"}});
  EXPECT_EQ(ks.get_expire("k"), now + 100'000);
  play(table, {{{"SET", "k", "v3"}, "+OK\r\n"}});
  EXPECT_EQ(ks.get_expire("k"), std::nullopt);
  play(table, {{{"SET", "k", "v", "PX", "1500"}, "+OK\r\n"}});
  EXPECT_EQ(ks.get_expire("k"), now + 1500);
  play(table, {{{"SET", "k", "v", "PXAT", std::to_string(now + 7)}, "+OK\r\n"}});
  EXPECT_EQ(ks.get_expire("k"), now + 7);
  play(table, {{{"SET", "k", "v", "EXAT", std::to_string(now / 1000 + 60)}, "+OK\r\n"}});
  EXPECT_EQ(ks.get_expire("k"), (now / 1000 + 60) * 1000);
}

// Redis: an absolute time in the past stores the key and expires it at once.
TEST_F(Commands, SetWithATimeInThePast) {
  play(table, {
      {{"SET", "k", "v", "PXAT", "1"}, "+OK\r\n"},
      {{"EXISTS", "k"}, ":0\r\n"},
      {{"SET", "k", "v", "EXAT", "1", "GET"}, "$-1\r\n"},
      {{"EXISTS", "k"}, ":0\r\n"},
  });
}

TEST_F(Commands, KeysExpireWithTheClock) {
  play(table, {{{"SET", "k", "v", "PX", "100"}, "+OK\r\n"}});
  clock.advance(99);
  play(table, {{{"GET", "k"}, "$1\r\nv\r\n"}});
  clock.advance(1);
  play(table, {{{"GET", "k"}, "$-1\r\n"}, {{"EXISTS", "k"}, ":0\r\n"}});
}

// INCR and APPEND modify in place and keep the TTL; MSET replaces and drops it.
TEST_F(Commands, InPlaceWritesKeepTheTtl) {
  play(table, {
      {{"SET", "n", "1", "EX", "100"}, "+OK\r\n"},
      {{"INCR", "n"}, ":2\r\n"},
      {{"APPEND", "n", "0"}, ":2\r\n"},
  });
  EXPECT_TRUE(ks.get_expire("n").has_value());
  play(table, {{{"MSET", "n", "5"}, "+OK\r\n"}});
  EXPECT_FALSE(ks.get_expire("n").has_value());
}

// A few of db/strings' transcripts, to check nothing was lost in the port.
TEST_F(Commands, StringsStillWork) {
  play(table, {
      {{"SET", "k", "v", "NX"}, "+OK\r\n"},
      {{"SET", "k", "v2", "XX"}, "+OK\r\n"},
      {{"SET", "k", "v", "NX", "XX"}, "-ERR syntax error\r\n"},
      {{"SET", "k", "v3", "xx", "get"}, "$2\r\nv2\r\n"},
      {{"SET", "big", "9223372036854775807"}, "+OK\r\n"},
      {{"INCR", "big"}, "-ERR increment or decrement would overflow\r\n"},
      {{"SET", "z", "0"}, "+OK\r\n"},
      {{"DECRBY", "z", "-9223372036854775808"}, "-ERR decrement would overflow\r\n"},
      {{"SET", "s", "01"}, "+OK\r\n"},
      {{"INCR", "s"}, "-ERR value is not an integer or out of range\r\n"},
      {{"MSET", "a", "1", "b", "2"}, "+OK\r\n"},
      {{"MGET", "a", "b", "c"}, "*3\r\n$1\r\n1\r\n$1\r\n2\r\n$-1\r\n"},
      {{"MSET", "a", "1", "b"}, "-ERR wrong number of arguments for 'mset' command\r\n"},
      {{"EXISTS", "a", "a", "missing"}, ":2\r\n"},
      {{"DEL", "a", "a"}, ":1\r\n"},
      {{"APPEND", "x", "hello"}, ":5\r\n"},
      {{"STRLEN", "x"}, ":5\r\n"},
  });
}

}  // namespace
}  // namespace rd
