#include <gtest/gtest.h>

#include "db/lists/lists.h"

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
    register_list_optional_commands(table, ks);
  }
  FakeClock clock;
  Keyspace ks{clock};
  CommandTable table;
};

TEST_F(Lists, LremAndLinsert) {
  play(table, {
      {{"RPUSH", "l", "a", "b", "a", "c", "a"}, ":5\r\n"},
      {{"LREM", "l", "2", "a"}, ":2\r\n"},
      {{"LRANGE", "l", "0", "-1"}, "*3\r\n$1\r\nb\r\n$1\r\nc\r\n$1\r\na\r\n"},
      {{"RPUSH", "m", "a", "b", "a", "c", "a"}, ":5\r\n"},
      {{"LREM", "m", "-2", "a"}, ":2\r\n"},
      {{"LRANGE", "m", "0", "-1"}, "*3\r\n$1\r\na\r\n$1\r\nb\r\n$1\r\nc\r\n"},
      {{"LREM", "m", "0", "a"}, ":1\r\n"},
      {{"LRANGE", "m", "0", "-1"}, "*2\r\n$1\r\nb\r\n$1\r\nc\r\n"},
      {{"LREM", "missing", "0", "a"}, ":0\r\n"},
      {{"LINSERT", "m", "BEFORE", "c", "X"}, ":3\r\n"},
      {{"LINSERT", "m", "AFTER", "c", "Y"}, ":4\r\n"},
      {{"LINSERT", "m", "AFTER", "nothere", "Y"}, ":-1\r\n"},
      {{"LRANGE", "m", "0", "-1"}, "*4\r\n$1\r\nb\r\n$1\r\nX\r\n$1\r\nc\r\n$1\r\nY\r\n"},
      {{"LINSERT", "missing", "BEFORE", "a", "b"}, ":0\r\n"},
      {{"LINSERT", "m", "MIDDLE", "c", "Z"}, "-ERR syntax error\r\n"},
  });
}
}  // namespace
}  // namespace rd
