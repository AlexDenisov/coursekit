#include "db/lists/lists.h"

#include <gtest/gtest.h>

#include "common/transcript.h"

// Transcripts recorded from Redis 8.10: `python3 tools/redis_reference.py
// tools/cases/lists.txt`.

namespace rd {
namespace {

using testing::play;

class Lists : public ::testing::Test {
 protected:
  Lists() {
    register_keyspace_commands(table, ks);
    register_list_commands(table, ks);
    
  }
  FakeClock clock;
  Keyspace ks{clock};
  CommandTable table;
};

TEST_F(Lists, PushAndRange) {
  play(table, {
      {{"LPUSH", "l", "a"}, ":1\r\n"},
      {{"LPUSH", "l", "b", "c"}, ":3\r\n"},
      {{"RPUSH", "l", "x", "y"}, ":5\r\n"},
      {{"LRANGE", "l", "0", "-1"}, "*5\r\n$1\r\nc\r\n$1\r\nb\r\n$1\r\na\r\n$1\r\nx\r\n$1\r\ny\r\n"},
      {{"LLEN", "l"}, ":5\r\n"},
      {{"LLEN", "missing"}, ":0\r\n"},
      {{"LRANGE", "missing", "0", "-1"}, "*0\r\n"},
      {{"LRANGE", "l", "1", "2"}, "*2\r\n$1\r\nb\r\n$1\r\na\r\n"},
      {{"LRANGE", "l", "-2", "-1"}, "*2\r\n$1\r\nx\r\n$1\r\ny\r\n"},
      {{"LRANGE", "l", "-100", "100"}, "*5\r\n$1\r\nc\r\n$1\r\nb\r\n$1\r\na\r\n$1\r\nx\r\n$1\r\ny\r\n"},
      {{"LRANGE", "l", "3", "1"}, "*0\r\n"},
      {{"LRANGE", "l", "5", "10"}, "*0\r\n"},
      {{"LRANGE", "l", "-1", "-5"}, "*0\r\n"},
      {{"LRANGE", "l", "a", "1"}, "-ERR value is not an integer or out of range\r\n"},
      {{"LPUSH"}, "-ERR wrong number of arguments for 'lpush' command\r\n"},
      {{"LPUSH", "l"}, "-ERR wrong number of arguments for 'lpush' command\r\n"},
  });
}

TEST_F(Lists, Pop) {
  play(table, {
      {{"RPUSH", "l", "a", "b", "c", "d", "e"}, ":5\r\n"},
      {{"LPOP", "l"}, "$1\r\na\r\n"},
      {{"RPOP", "l"}, "$1\r\ne\r\n"},
      {{"LPOP", "l", "2"}, "*2\r\n$1\r\nb\r\n$1\r\nc\r\n"},
      {{"RPOP", "l", "5"}, "*1\r\n$1\r\nd\r\n"},
      {{"EXISTS", "l"}, ":0\r\n"},
      {{"LPOP", "l"}, "$-1\r\n"},
      {{"RPOP", "missing"}, "$-1\r\n"},
      {{"LPOP", "missing", "2"}, "*-1\r\n"},
      {{"RPUSH", "l", "a"}, ":1\r\n"},
      {{"LPOP", "l", "0"}, "*0\r\n"},
      {{"LPOP", "l", "-1"}, "-ERR value is out of range, must be positive\r\n"},
      {{"LPOP", "l", "abc"}, "-ERR value is out of range, must be positive\r\n"},
      {{"LPOP", "l", "1", "2"}, "-ERR wrong number of arguments for 'lpop' command\r\n"},
  });
}

TEST_F(Lists, IndexAndSet) {
  play(table, {
      {{"RPUSH", "l", "a", "b", "c"}, ":3\r\n"},
      {{"LINDEX", "l", "0"}, "$1\r\na\r\n"},
      {{"LINDEX", "l", "-1"}, "$1\r\nc\r\n"},
      {{"LINDEX", "l", "3"}, "$-1\r\n"},
      {{"LINDEX", "l", "-4"}, "$-1\r\n"},
      {{"LINDEX", "missing", "0"}, "$-1\r\n"},
      {{"LINDEX", "l", "x"}, "-ERR value is not an integer or out of range\r\n"},
      {{"LSET", "l", "1", "B"}, "+OK\r\n"},
      {{"LSET", "l", "-1", "C"}, "+OK\r\n"},
      {{"LRANGE", "l", "0", "-1"}, "*3\r\n$1\r\na\r\n$1\r\nB\r\n$1\r\nC\r\n"},
      {{"LSET", "l", "3", "z"}, "-ERR index out of range\r\n"},
      {{"LSET", "missing", "0", "z"}, "-ERR no such key\r\n"},
  });
}

TEST_F(Lists, Trim) {
  play(table, {
      {{"RPUSH", "l", "a", "b", "c", "d", "e"}, ":5\r\n"},
      {{"LTRIM", "l", "1", "-2"}, "+OK\r\n"},
      {{"LRANGE", "l", "0", "-1"}, "*3\r\n$1\r\nb\r\n$1\r\nc\r\n$1\r\nd\r\n"},
      {{"LTRIM", "l", "5", "10"}, "+OK\r\n"},
      {{"EXISTS", "l"}, ":0\r\n"},
      {{"LTRIM", "missing", "0", "1"}, "+OK\r\n"},
      {{"RPUSH", "l", "a", "b", "c"}, ":3\r\n"},
      {{"LTRIM", "l", "-100", "100"}, "+OK\r\n"},
      {{"LRANGE", "l", "0", "-1"}, "*3\r\n$1\r\na\r\n$1\r\nb\r\n$1\r\nc\r\n"},
  });
}

TEST_F(Lists, PushxNeedsAnExistingList) {
  play(table, {
      {{"LPUSHX", "l", "a"}, ":0\r\n"},
      {{"RPUSHX", "l", "a"}, ":0\r\n"},
      {{"EXISTS", "l"}, ":0\r\n"},
      {{"RPUSH", "l", "a"}, ":1\r\n"},
      {{"LPUSHX", "l", "b"}, ":2\r\n"},
      {{"RPUSHX", "l", "c", "d"}, ":4\r\n"},
      {{"LRANGE", "l", "0", "-1"}, "*4\r\n$1\r\nb\r\n$1\r\na\r\n$1\r\nc\r\n$1\r\nd\r\n"},
  });
}

TEST_F(Lists, WrongType) {
  play(table, {
      {{"SET", "s", "v"}, "+OK\r\n"},
      {{"LPUSH", "s", "a"}, "-WRONGTYPE Operation against a key holding the wrong kind of value\r\n"},
      {{"LRANGE", "s", "0", "-1"}, "-WRONGTYPE Operation against a key holding the wrong kind of value\r\n"},
      {{"LLEN", "s"}, "-WRONGTYPE Operation against a key holding the wrong kind of value\r\n"},
      {{"LPOP", "s"}, "-WRONGTYPE Operation against a key holding the wrong kind of value\r\n"},
      {{"RPUSH", "l", "a"}, ":1\r\n"},
      {{"GET", "l"}, "-WRONGTYPE Operation against a key holding the wrong kind of value\r\n"},
  });
}

// The pitfall: a list with no elements left must not linger as an empty value.
TEST_F(Lists, PoppingTheLastElementDeletesTheKey) {
  play(table, {
      {{"RPUSH", "l", "a"}, ":1\r\n"},
      {{"RPOP", "l"}, "$1\r\na\r\n"},
      {{"EXISTS", "l"}, ":0\r\n"},
      {{"TYPE", "l"}, "+none\r\n"},
  });
  EXPECT_EQ(ks.size(), 0u);
}

// A deque gives O(1) at both ends; LPUSH of 200 000 elements one at a time must not be
// quadratic (a std::vector with insert-at-front would be).
TEST_F(Lists, PushingAtTheHeadIsCheap) {
  std::string reply;
  for (int i = 0; i < 200000; ++i) table.dispatch(std::vector<std::string>{"LPUSH", "l", "x"}, reply);
  reply.clear();
  table.dispatch(std::vector<std::string>{"LLEN", "l"}, reply);
  EXPECT_EQ(reply, ":200000\r\n");
}

}  // namespace
}  // namespace rd
