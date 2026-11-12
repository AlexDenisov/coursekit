#include "db/sets/sets.h"

#include <gtest/gtest.h>

#include <chrono>
#include <string>
#include <vector>

#include "common/transcript.h"

// Transcripts recorded from Redis 8.10: `python3 tools/redis_reference.py
// tools/cases/sets.txt`. Replies without a defined order are compared as multisets.

namespace rd {
namespace {

using testing::play;

class Sets : public ::testing::Test {
 protected:
  Sets() {
    register_keyspace_commands(table, ks);
    register_set_commands(table, ks);
  }
  void sadd_many(const std::string& key, int from, int to) {
    std::vector<std::string> args = {"SADD", key};
    for (int i = from; i < to; ++i) args.push_back(std::to_string(i));
    std::string reply;
    table.dispatch(args, reply);
  }
  FakeClock clock;
  Keyspace ks{clock};
  CommandTable table;
};

TEST_F(Sets, AddAndMembers) {
  play(table, {
      {{"SADD", "s", "a", "b", "c"}, ":3\r\n"},
      {{"SADD", "s", "a", "d"}, ":1\r\n"},
      {{"SCARD", "s"}, ":4\r\n"},
      {{"SCARD", "missing"}, ":0\r\n"},
      {{"SISMEMBER", "s", "a"}, ":1\r\n"},
      {{"SISMEMBER", "s", "z"}, ":0\r\n"},
      {{"SISMEMBER", "missing", "a"}, ":0\r\n"},
      {{"SMISMEMBER", "s", "a", "z", "b"}, "*3\r\n:1\r\n:0\r\n:1\r\n"},
      {{"SMEMBERS", "s"}, "*4\r\n$1\r\na\r\n$1\r\nb\r\n$1\r\nc\r\n$1\r\nd\r\n", testing::kAnyOrder},
      {{"SMEMBERS", "missing"}, "*0\r\n", testing::kAnyOrder},
      {{"SADD", "s"}, "-ERR wrong number of arguments for 'sadd' command\r\n"},
  });
}

TEST_F(Sets, RemovingTheLastMemberDeletesTheKey) {
  play(table, {
      {{"SADD", "s", "a", "b", "c"}, ":3\r\n"},
      {{"SREM", "s", "a", "z"}, ":1\r\n"},
      {{"SREM", "s", "b", "c"}, ":2\r\n"},
      {{"EXISTS", "s"}, ":0\r\n"},
      {{"SREM", "missing", "a"}, ":0\r\n"},
  });
}

TEST_F(Sets, Algebra) {
  play(table, {
      {{"SADD", "a", "1", "2", "3", "4"}, ":4\r\n"},
      {{"SADD", "b", "3", "4", "5"}, ":3\r\n"},
      {{"SADD", "c", "4", "5", "6"}, ":3\r\n"},
      {{"SINTER", "a", "b"}, "*2\r\n$1\r\n3\r\n$1\r\n4\r\n", testing::kAnyOrder},
      {{"SINTER", "a", "b", "c"}, "*1\r\n$1\r\n4\r\n", testing::kAnyOrder},
      {{"SINTER", "a", "missing"}, "*0\r\n", testing::kAnyOrder},
      {{"SUNION", "a", "b"}, "*5\r\n$1\r\n1\r\n$1\r\n2\r\n$1\r\n3\r\n$1\r\n4\r\n$1\r\n5\r\n", testing::kAnyOrder},
      {{"SUNION", "a", "missing"}, "*4\r\n$1\r\n1\r\n$1\r\n2\r\n$1\r\n3\r\n$1\r\n4\r\n", testing::kAnyOrder},
      {{"SDIFF", "a", "b"}, "*2\r\n$1\r\n1\r\n$1\r\n2\r\n", testing::kAnyOrder},
      {{"SDIFF", "a", "b", "c"}, "*2\r\n$1\r\n1\r\n$1\r\n2\r\n", testing::kAnyOrder},
      {{"SDIFF", "missing", "a"}, "*0\r\n", testing::kAnyOrder},
      {{"SINTERSTORE", "dst", "a", "b"}, ":2\r\n"},
      {{"SMEMBERS", "dst"}, "*2\r\n$1\r\n3\r\n$1\r\n4\r\n", testing::kAnyOrder},
      {{"SUNIONSTORE", "dst", "a", "c"}, ":6\r\n"},
      {{"SCARD", "dst"}, ":6\r\n"},
      {{"SDIFFSTORE", "dst", "a", "b", "c"}, ":2\r\n"},
      {{"SMEMBERS", "dst"}, "*2\r\n$1\r\n1\r\n$1\r\n2\r\n", testing::kAnyOrder},
      {{"SINTERSTORE", "dst", "a", "missing"}, ":0\r\n"},
      {{"EXISTS", "dst"}, ":0\r\n"},
  });
}

TEST_F(Sets, Smove) {
  play(table, {
      {{"SADD", "src", "a", "b"}, ":2\r\n"},
      {{"SADD", "dst", "c"}, ":1\r\n"},
      {{"SMOVE", "src", "dst", "a"}, ":1\r\n"},
      {{"SMOVE", "src", "dst", "z"}, ":0\r\n"},
      {{"SMEMBERS", "src"}, "*1\r\n$1\r\nb\r\n", testing::kAnyOrder},
      {{"SMEMBERS", "dst"}, "*2\r\n$1\r\nc\r\n$1\r\na\r\n", testing::kAnyOrder},
      {{"SMOVE", "src", "dst", "b"}, ":1\r\n"},
      {{"EXISTS", "src"}, ":0\r\n"},
      {{"SMOVE", "missing", "dst", "a"}, ":0\r\n"},
  });
}

TEST_F(Sets, WrongType) {
  play(table, {
      {{"SET", "str", "v"}, "+OK\r\n"},
      {{"SADD", "str", "a"}, "-WRONGTYPE Operation against a key holding the wrong kind of value\r\n"},
      {{"SINTER", "str"}, "-WRONGTYPE Operation against a key holding the wrong kind of value\r\n", testing::kAnyOrder},
      {{"SADD", "s", "a"}, ":1\r\n"},
      {{"SINTER", "s", "str"}, "-WRONGTYPE Operation against a key holding the wrong kind of value\r\n", testing::kAnyOrder},
      {{"SUNION", "s", "str"}, "-WRONGTYPE Operation against a key holding the wrong kind of value\r\n", testing::kAnyOrder},
      {{"SDIFF", "str", "s"}, "-WRONGTYPE Operation against a key holding the wrong kind of value\r\n", testing::kAnyOrder},
      {{"SINTERSTORE", "dst", "s", "str"}, "-WRONGTYPE Operation against a key holding the wrong kind of value\r\n"},
  });
}

// The pitfall: SINTER of a 1-element set with a 1 000 000-element set. Iterating the big
// one costs a million lookups; iterating the small one costs one. Redis sorts the sets by
// size first.
TEST_F(Sets, IntersectionIteratesTheSmallestSet) {
  sadd_many("big", 0, 1000000);
  sadd_many("small", 999999, 1000000);
  auto t0 = std::chrono::steady_clock::now();
  for (int i = 0; i < 200; ++i) {
    play(table, {{{"SINTER", "big", "small"}, "*1\r\n$6\r\n999999\r\n"}});
  }
  auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();
  EXPECT_LT(ms, 500) << "200 intersections took " << ms << " ms";
}

}  // namespace
}  // namespace rd
