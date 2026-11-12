#include "db/strings/strings.h"

#include <gtest/gtest.h>

#include "common/transcript.h"

// Every transcript below was recorded from Redis 8.10 by
// `python3 tools/redis_reference.py tools/cases/strings.txt`; each test starts from an
// empty database, like each "##" group there.

namespace rd {
namespace {

using testing::play;

class Strings : public ::testing::Test {
 protected:
  Strings() {
    register_connection_commands(table);
    register_string_commands(table, db);
  }
  StringDb db;
  CommandTable table;
};

TEST_F(Strings, SetAndGet) {
  play(table, {
      {{"SET", "k", "v"}, "+OK\r\n"},
      {{"GET", "k"}, "$1\r\nv\r\n"},
      {{"SET", "k", "w"}, "+OK\r\n"},
      {{"GET", "k"}, "$1\r\nw\r\n"},
      {{"GET", "missing"}, "$-1\r\n"},
      {{"SET"}, "-ERR wrong number of arguments for 'set' command\r\n"},
      {{"SET", "k"}, "-ERR wrong number of arguments for 'set' command\r\n"},
      {{"GET", "a", "b"}, "-ERR wrong number of arguments for 'get' command\r\n"},
  });
}

TEST_F(Strings, EmptyValueIsNotMissing) {
  play(table, {
      {{"SET", "k", "v"}, "+OK\r\n"},
      {{"SET", "k", ""}, "+OK\r\n"},
      {{"GET", "k"}, "$0\r\n\r\n"},
      {{"EXISTS", "k"}, ":1\r\n"},
      {{"STRLEN", "k"}, ":0\r\n"},
  });
}

TEST_F(Strings, ValuesAreBinarySafe) {
  play(table, {
      {{"SET", "bin", std::string("\0\1\r\n", 4)}, "+OK\r\n"},
      {{"GET", "bin"}, std::string("$4\r\n\0\1\r\n\r\n", 10)},
      {{"STRLEN", "bin"}, ":4\r\n"},
  });
}

TEST_F(Strings, SetOptions) {
  play(table, {
      {{"SET", "k", "v", "NX"}, "+OK\r\n"},
      {{"SET", "n", "v", "NX"}, "+OK\r\n"},
      {{"SET", "k", "v2", "XX"}, "+OK\r\n"},
      {{"GET", "k"}, "$2\r\nv2\r\n"},
      {{"SET", "x", "v", "XX"}, "$-1\r\n"},
      {{"GET", "x"}, "$-1\r\n"},
      {{"SET", "k", "v", "NX", "XX"}, "-ERR syntax error\r\n"},
      {{"SET", "k", "v", "nx"}, "$-1\r\n"},
      {{"SET", "k", "v", "FOO"}, "-ERR syntax error\r\n"},
      {{"SET", "k", "v", "GET"}, "$2\r\nv2\r\n"},
      {{"SET", "new", "v", "GET"}, "$-1\r\n"},
      {{"SET", "k", "v3", "xx", "get"}, "$1\r\nv\r\n"},
      {{"SET", "k", "v4", "NX", "GET"}, "$2\r\nv3\r\n"},
      {{"SET", "k", "v4", "GET", "GET"}, "$2\r\nv3\r\n"},
  });
}

// A rejected SET must not have changed anything.
TEST_F(Strings, SyntaxErrorLeavesTheValueAlone) {
  play(table, {
      {{"SET", "k", "old"}, "+OK\r\n"},
      {{"SET", "k", "new", "NX", "XX"}, "-ERR syntax error\r\n"},
      {{"SET", "k", "new", "EXX"}, "-ERR syntax error\r\n"},
      {{"GET", "k"}, "$3\r\nold\r\n"},
  });
}

TEST_F(Strings, DelAndExists) {
  play(table, {
      {{"SET", "a", "1"}, "+OK\r\n"},
      {{"SET", "b", "2"}, "+OK\r\n"},
      {{"DEL", "a", "b", "c"}, ":2\r\n"},
      {{"DEL", "b"}, ":0\r\n"},
      {{"EXISTS", "a"}, ":0\r\n"},
      {{"SET", "a", "1"}, "+OK\r\n"},
      {{"DEL"}, "-ERR wrong number of arguments for 'del' command\r\n"},
      {{"EXISTS"}, "-ERR wrong number of arguments for 'exists' command\r\n"},
  });
}

// The asymmetry: EXISTS counts arguments, DEL counts keys.
TEST_F(Strings, ExistsCountsRepeatsButDelDoesNot) {
  play(table, {
      {{"SET", "a", "1"}, "+OK\r\n"},
      {{"EXISTS", "a", "a", "missing"}, ":2\r\n"},
      {{"DEL", "a", "a"}, ":1\r\n"},
  });
}

TEST_F(Strings, IncrFamily) {
  play(table, {
      {{"INCR", "n"}, ":1\r\n"},
      {{"INCR", "n"}, ":2\r\n"},
      {{"INCRBY", "n", "10"}, ":12\r\n"},
      {{"DECR", "n"}, ":11\r\n"},
      {{"DECRBY", "n", "5"}, ":6\r\n"},
      {{"INCRBY", "n", "-20"}, ":-14\r\n"},
      {{"GET", "n"}, "$3\r\n-14\r\n"},
  });
}

// Integers are stored as text, so APPEND and INCR mix.
TEST_F(Strings, AppendThenIncr) {
  play(table, {
      {{"SET", "num", "10"}, "+OK\r\n"},
      {{"APPEND", "num", "5"}, ":3\r\n"},
      {{"INCR", "num"}, ":106\r\n"},
  });
}

TEST_F(Strings, IncrNeedsAStrictInteger) {
  const std::string err = "-ERR value is not an integer or out of range\r\n";
  play(table, {
      {{"SET", "s", "abc"}, "+OK\r\n"}, {{"INCR", "s"}, err},
      {{"SET", "s", " 1"}, "+OK\r\n"},  {{"INCR", "s"}, err},
      {{"SET", "s", "01"}, "+OK\r\n"},  {{"INCR", "s"}, err},
      {{"SET", "s", "+1"}, "+OK\r\n"},  {{"INCR", "s"}, err},
      {{"SET", "s", "-0"}, "+OK\r\n"},  {{"INCR", "s"}, err},
      {{"SET", "s", ""}, "+OK\r\n"},    {{"INCR", "s"}, err},
      {{"SET", "s", "1.5"}, "+OK\r\n"}, {{"INCR", "s"}, err},
      {{"GET", "s"}, "$3\r\n1.5\r\n"},
      {{"INCRBY", "n", "abc"}, err},
      {{"INCRBY", "n", "1.0"}, err},
      {{"INCRBY", "n", "9223372036854775808"}, err},
      {{"EXISTS", "n"}, ":0\r\n"},
  });
}

// The pitfall: check before adding. Signed overflow is undefined behaviour in C++, and
// a failed INCR must leave the value as it was.
TEST_F(Strings, IncrOverflowIsAnError) {
  play(table, {
      {{"SET", "big", "9223372036854775807"}, "+OK\r\n"},
      {{"INCR", "big"}, "-ERR increment or decrement would overflow\r\n"},
      {{"GET", "big"}, "$19\r\n9223372036854775807\r\n"},
      {{"SET", "big", "9223372036854775806"}, "+OK\r\n"},
      {{"INCR", "big"}, ":9223372036854775807\r\n"},
      {{"SET", "small", "-9223372036854775807"}, "+OK\r\n"},
      {{"DECR", "small"}, ":-9223372036854775808\r\n"},
      {{"DECR", "small"}, "-ERR increment or decrement would overflow\r\n"},
      {{"GET", "small"}, "$20\r\n-9223372036854775808\r\n"},
  });
}

// The pitfall inside the pitfall: DECRBY n is INCRBY -n, and -INT64_MIN doesn't exist.
TEST_F(Strings, DecrbyInt64MinIsItsOwnError) {
  play(table, {
      {{"SET", "z", "0"}, "+OK\r\n"},
      {{"DECRBY", "z", "-9223372036854775808"}, "-ERR decrement would overflow\r\n"},
      {{"DECRBY", "z", "9223372036854775807"}, ":-9223372036854775807\r\n"},
      {{"INCRBY", "z", "-9223372036854775808"}, "-ERR increment or decrement would overflow\r\n"},
      {{"SET", "z", "1"}, "+OK\r\n"},
      {{"INCRBY", "z", "-9223372036854775808"}, ":-9223372036854775807\r\n"},
  });
}

TEST_F(Strings, IncrAtTheInt64Limits) {
  play(table, {
      {{"SET", "y", "9223372036854775808"}, "+OK\r\n"},
      {{"INCR", "y"}, "-ERR value is not an integer or out of range\r\n"},
      {{"SET", "y", "-9223372036854775808"}, "+OK\r\n"},
      {{"INCR", "y"}, ":-9223372036854775807\r\n"},
      {{"SET", "y", "-9223372036854775809"}, "+OK\r\n"},
      {{"INCR", "y"}, "-ERR value is not an integer or out of range\r\n"},
  });
}

TEST_F(Strings, AppendAndStrlen) {
  play(table, {
      {{"APPEND", "a", "hello"}, ":5\r\n"},
      {{"APPEND", "a", " world"}, ":11\r\n"},
      {{"GET", "a"}, "$11\r\nhello world\r\n"},
      {{"STRLEN", "a"}, ":11\r\n"},
      {{"STRLEN", "missing"}, ":0\r\n"},
  });
}

TEST_F(Strings, MgetAndMset) {
  play(table, {
      {{"MSET", "a", "1", "b", "2"}, "+OK\r\n"},
      {{"MGET", "a", "b", "c"}, "*3\r\n$1\r\n1\r\n$1\r\n2\r\n$-1\r\n"},
      {{"MSET", "a"}, "-ERR wrong number of arguments for 'mset' command\r\n"},
      {{"MSET", "a", "1", "b"}, "-ERR wrong number of arguments for 'mset' command\r\n"},
      {{"MSET", "a", "3", "a", "4"}, "+OK\r\n"},
      {{"GET", "a"}, "$1\r\n4\r\n"},
      {{"MGET"}, "-ERR wrong number of arguments for 'mget' command\r\n"},
  });
}

TEST_F(Strings, CommandNamesIgnoreCase) {
  play(table, {
      {{"set", "k", "1"}, "+OK\r\n"},
      {{"InCr", "k"}, ":2\r\n"},
      {{"get", "k"}, "$1\r\n2\r\n"},
  });
}

}  // namespace
}  // namespace rd
