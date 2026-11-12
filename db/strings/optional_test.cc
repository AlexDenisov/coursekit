#include <gtest/gtest.h>

#include "db/strings/strings.h"
#include "db/strings/transcript.h"

// Recorded from Redis 8.10 by `python3 tools/redis_reference.py tools/cases/strings.txt`
// (the "getrange and setrange" and "range edge cases" groups).

namespace rd {
namespace {

using testing::play;

class Ranges : public ::testing::Test {
 protected:
  Ranges() {
    register_string_commands(table, db);
    register_string_range_commands(table, db);
  }
  StringDb db;
  CommandTable table;
};

TEST_F(Ranges, Getrange) {
  play(table, {
      {{"SET", "g", "Hello World"}, "+OK\r\n"},
      {{"GETRANGE", "g", "0", "4"}, "$5\r\nHello\r\n"},
      {{"GETRANGE", "g", "-5", "-1"}, "$5\r\nWorld\r\n"},
      {{"GETRANGE", "g", "0", "0"}, "$1\r\nH\r\n"},
      {{"GETRANGE", "g", "3", "3"}, "$1\r\nl\r\n"},
      {{"GETRANGE", "g", "0", "100"}, "$11\r\nHello World\r\n"},
      {{"GETRANGE", "missing", "0", "-1"}, "$0\r\n\r\n"},
      {{"GETRANGE", "g", "a", "1"}, "-ERR value is not an integer or out of range\r\n"},
  });
}

// The pitfall: four clamping rules, applied in the right order.
TEST_F(Ranges, GetrangeClamping) {
  play(table, {
      {{"SET", "g", "Hello World"}, "+OK\r\n"},
      {{"GETRANGE", "g", "-100", "2"}, "$3\r\nHel\r\n"},
      {{"GETRANGE", "g", "5", "1"}, "$0\r\n\r\n"},
      {{"GETRANGE", "g", "100", "200"}, "$0\r\n\r\n"},
      {{"GETRANGE", "g", "-1", "-5"}, "$0\r\n\r\n"},
      {{"GETRANGE", "g", "0", "-100"}, "$1\r\nH\r\n"},
      {{"GETRANGE", "g", "-5", "-100"}, "$0\r\n\r\n"},
      {{"GETRANGE", "g", "-100", "-5"}, "$7\r\nHello W\r\n"},
      {{"SET", "e", ""}, "+OK\r\n"},
      {{"GETRANGE", "e", "0", "0"}, "$0\r\n\r\n"},
      {{"GETRANGE", "e", "0", "-1"}, "$0\r\n\r\n"},
  });
}

TEST_F(Ranges, Setrange) {
  play(table, {
      {{"SET", "g", "Hello World"}, "+OK\r\n"},
      {{"SETRANGE", "g", "6", "Redis"}, ":11\r\n"},
      {{"GET", "g"}, "$11\r\nHello Redis\r\n"},
      {{"SETRANGE", "g", "0", "Jello"}, ":11\r\n"},
      {{"SETRANGE", "g", "11", "!"}, ":12\r\n"},
      {{"GET", "g"}, "$12\r\nJello Redis!\r\n"},
      {{"SETRANGE", "g", "-1", "x"}, "-ERR offset is out of range\r\n"},
      {{"SETRANGE", "g", "abc", "x"}, "-ERR value is not an integer or out of range\r\n"},
  });
}

TEST_F(Ranges, SetrangePadsWithZeroBytes) {
  play(table, {
      {{"SETRANGE", "pad", "5", "x"}, ":6\r\n"},
      {{"GET", "pad"}, std::string("$6\r\n\0\0\0\0\0x\r\n", 12)},
  });
}

TEST_F(Ranges, SetrangeWithEmptyValueChangesNothing) {
  play(table, {
      {{"SET", "g", "Hello World"}, "+OK\r\n"},
      {{"SETRANGE", "g", "0", ""}, ":11\r\n"},
      {{"SETRANGE", "nokey", "3", ""}, ":0\r\n"},
      {{"EXISTS", "nokey"}, ":0\r\n"},
      {{"SETRANGE", "g", "536870912", ""}, ":11\r\n"},
      {{"SETRANGE", "g", "-1", ""}, "-ERR offset is out of range\r\n"},
  });
}

// The pitfall: one command must not be able to make the server allocate 512 MB+.
TEST_F(Ranges, SetrangeBeyond512MbIsRejectedBeforeAllocating) {
  play(table, {
      {{"SET", "g", "Hello World"}, "+OK\r\n"},
      {{"SETRANGE", "g", "536870912", "x"},
       "-ERR string exceeds maximum allowed size (proto-max-bulk-len)\r\n"},
      {{"SETRANGE", "h", "9223372036854775807", "x"},
       "-ERR string exceeds maximum allowed size (proto-max-bulk-len)\r\n"},
      {{"GET", "g"}, "$11\r\nHello World\r\n"},
      {{"EXISTS", "h"}, ":0\r\n"},
  });
}

}  // namespace
}  // namespace rd
