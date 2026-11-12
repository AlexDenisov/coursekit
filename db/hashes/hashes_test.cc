#include "db/hashes/hashes.h"

#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "common/transcript.h"

// Transcripts recorded from Redis 8.10: `python3 tools/redis_reference.py
// tools/cases/hashes.txt`. Replies without a defined order are compared as multisets.

namespace rd {
namespace {

using testing::play;

class Hashes : public ::testing::Test {
 protected:
  Hashes() {
    register_keyspace_commands(table, ks);
    register_hash_commands(table, ks);
  }
  FakeClock clock;
  Keyspace ks{clock};
  CommandTable table;
};

TEST_F(Hashes, SetAndGet) {
  play(table, {
      {{"HSET", "h", "f1", "v1"}, ":1\r\n"},
      {{"HSET", "h", "f2", "v2", "f3", "v3"}, ":2\r\n"},
      {{"HSET", "h", "f1", "new"}, ":0\r\n"},
      {{"HGET", "h", "f1"}, "$3\r\nnew\r\n"},
      {{"HGET", "h", "nope"}, "$-1\r\n"},
      {{"HGET", "missing", "f"}, "$-1\r\n"},
      {{"HLEN", "h"}, ":3\r\n"},
      {{"HLEN", "missing"}, ":0\r\n"},
      {{"HEXISTS", "h", "f2"}, ":1\r\n"},
      {{"HEXISTS", "h", "nope"}, ":0\r\n"},
      {{"HSTRLEN", "h", "f1"}, ":3\r\n"},
      {{"HSTRLEN", "h", "nope"}, ":0\r\n"},
      {{"HMGET", "h", "f1", "nope", "f2"}, "*3\r\n$3\r\nnew\r\n$-1\r\n$2\r\nv2\r\n"},
      {{"HMGET", "missing", "a", "b"}, "*2\r\n$-1\r\n$-1\r\n"},
      {{"HSET", "h", "f"}, "-ERR wrong number of arguments for 'hset' command\r\n"},
      {{"HSET", "h", "a", "b", "c"}, "-ERR wrong number of arguments for 'hset' command\r\n"},
      {{"HSET"}, "-ERR wrong number of arguments for 'hset' command\r\n"},
  });
}

TEST_F(Hashes, GetAllKeysVals) {
  play(table, {
      {{"HSET", "h", "a", "1", "b", "2", "c", "3"}, ":3\r\n"},
      {{"HGETALL", "h"}, "*6\r\n$1\r\na\r\n$1\r\n1\r\n$1\r\nb\r\n$1\r\n2\r\n$1\r\nc\r\n$1\r\n3\r\n", testing::kAnyPairOrder},
      {{"HKEYS", "h"}, "*3\r\n$1\r\na\r\n$1\r\nb\r\n$1\r\nc\r\n", testing::kAnyOrder},
      {{"HVALS", "h"}, "*3\r\n$1\r\n1\r\n$1\r\n2\r\n$1\r\n3\r\n", testing::kAnyOrder},
      {{"HGETALL", "missing"}, "*0\r\n", testing::kAnyPairOrder},
      {{"HKEYS", "missing"}, "*0\r\n", testing::kAnyOrder},
  });
}

TEST_F(Hashes, DeletingTheLastFieldDeletesTheKey) {
  play(table, {
      {{"HSET", "h", "a", "1", "b", "2"}, ":2\r\n"},
      {{"HDEL", "h", "a", "nope"}, ":1\r\n"},
      {{"HDEL", "h", "a"}, ":0\r\n"},
      {{"HLEN", "h"}, ":1\r\n"},
      {{"HDEL", "h", "b"}, ":1\r\n"},
      {{"EXISTS", "h"}, ":0\r\n"},
      {{"HDEL", "missing", "a"}, ":0\r\n"},
  });
}

TEST_F(Hashes, SetnxAndIncrby) {
  play(table, {
      {{"HSETNX", "h", "f", "v"}, ":1\r\n"},
      {{"HSETNX", "h", "f", "w"}, ":0\r\n"},
      {{"HGET", "h", "f"}, "$1\r\nv\r\n"},
      {{"HINCRBY", "h", "n", "5"}, ":5\r\n"},
      {{"HINCRBY", "h", "n", "-2"}, ":3\r\n"},
      {{"HINCRBY", "h", "f", "1"}, "-ERR hash value is not an integer\r\n"},
      {{"HINCRBY", "h", "n", "abc"}, "-ERR value is not an integer or out of range\r\n"},
      {{"HSET", "h", "big", "9223372036854775807"}, ":1\r\n"},
      {{"HINCRBY", "h", "big", "1"}, "-ERR increment or decrement would overflow\r\n"},
      {{"HGET", "h", "n"}, "$1\r\n3\r\n"},
  });
}

TEST_F(Hashes, WrongType) {
  play(table, {
      {{"SET", "s", "v"}, "+OK\r\n"},
      {{"HSET", "s", "f", "v"}, "-WRONGTYPE Operation against a key holding the wrong kind of value\r\n"},
      {{"HGET", "s", "f"}, "-WRONGTYPE Operation against a key holding the wrong kind of value\r\n"},
      {{"HGETALL", "s"}, "-WRONGTYPE Operation against a key holding the wrong kind of value\r\n", testing::kAnyPairOrder},
      {{"HSET", "h", "f", "v"}, ":1\r\n"},
      {{"GET", "h"}, "-WRONGTYPE Operation against a key holding the wrong kind of value\r\n"},
      {{"TYPE", "h"}, "+hash\r\n"},
  });
}

// A big hash: your dict grows through many rehashes underneath HSET, and every field is
// still there.
TEST_F(Hashes, ManyFields) {
  std::string reply;
  for (int i = 0; i < 50000; ++i) {
    table.dispatch(std::vector<std::string>{"HSET", "h", "f" + std::to_string(i), std::to_string(i)}, reply);
  }
  play(table, {
      {{"HLEN", "h"}, ":50000\r\n"},
      {{"HGET", "h", "f12345"}, "$5\r\n12345\r\n"},
      {{"HGET", "h", "f50000"}, "$-1\r\n"},
  });
}

}  // namespace
}  // namespace rd
