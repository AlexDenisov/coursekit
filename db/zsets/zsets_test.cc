#include "db/zsets/zsets.h"

#include <gtest/gtest.h>

#include <cmath>
#include <limits>
#include <string>

#include "common/rng.h"
#include "common/transcript.h"

// Transcripts and score strings recorded from Redis 8.10: `python3
// tools/redis_reference.py tools/cases/zsets.txt`.

namespace rd {
namespace {

using testing::play;

// ---- score formatting (ZSCORE replies from the "score formatting" group) ----

TEST(FormatScore, Integers) {
  EXPECT_EQ(format_score(1), "1");
  EXPECT_EQ(format_score(-2), "-2");
  EXPECT_EQ(format_score(100), "100");
  EXPECT_EQ(format_score(1000), "1000");
  EXPECT_EQ(format_score(123456789012), "123456789012");
  EXPECT_EQ(format_score(1e15), "1000000000000000");
  EXPECT_EQ(format_score(1e16), "10000000000000000");
}

TEST(FormatScore, ZerosAndInfinities) {
  EXPECT_EQ(format_score(0.0), "0");
  EXPECT_EQ(format_score(-0.0), "0");
  EXPECT_EQ(format_score(std::numeric_limits<double>::infinity()), "inf");
  EXPECT_EQ(format_score(-std::numeric_limits<double>::infinity()), "-inf");
}

TEST(FormatScore, Fractions) {
  EXPECT_EQ(format_score(1.5), "1.5");
  EXPECT_EQ(format_score(0.1), "0.1");
  EXPECT_EQ(format_score(1e-5), "0.00001");
  EXPECT_EQ(format_score(0.000001), "0.000001");
  EXPECT_EQ(format_score(3.14159265358979), "3.14159265358979");
}

// The pitfall: 0.1 + 0.2. "%g" would print 0.3, which reads back as a different double.
TEST(FormatScore, ShortestDigitsThatRoundTrip) {
  EXPECT_EQ(format_score(0.30000000000000004), "0.30000000000000004");
  EXPECT_EQ(format_score(0.1 + 0.2), "0.30000000000000004");
}

TEST(FormatScore, BigAndSmall) {
  EXPECT_EQ(format_score(1e20), "1e+20");
  EXPECT_EQ(format_score(1e21), "1e+21");
  EXPECT_EQ(format_score(1e300), "1e+300");
  EXPECT_EQ(format_score(5e-324), "5e-324");
  EXPECT_EQ(format_score(1.7976931348623157e308), "1.7976931348623157e+308");
  // Integral but above 2^62: not printed as an integer, yet no exponent either.
  EXPECT_EQ(format_score(12345678901234567890.0), "12345678901234567000");
}

TEST(FormatScore, RoundTripsRandomDoubles) {
  Rng rng(5);
  for (int i = 0; i < 20000; ++i) {
    double v = std::ldexp(rng.uniform() * 2 - 1, static_cast<int>(rng.between(-60, 80)));
    std::string s = format_score(v);
    double back;
    ASSERT_TRUE(parse_score(s, &back)) << s;
    ASSERT_EQ(back, v) << s << " doesn't read back exactly";
  }
}

TEST(ParseScore, AcceptsWhatRedisAccepts) {
  double v;
  ASSERT_TRUE(parse_score("1.5", &v));
  EXPECT_EQ(v, 1.5);
  ASSERT_TRUE(parse_score("+inf", &v));
  EXPECT_TRUE(std::isinf(v) && v > 0);
  ASSERT_TRUE(parse_score("-inf", &v));
  EXPECT_TRUE(std::isinf(v) && v < 0);
  ASSERT_TRUE(parse_score("1.0e3", &v));
  EXPECT_EQ(v, 1000);
  ASSERT_TRUE(parse_score("0x10", &v));  // strtod reads hex; so does Redis
  EXPECT_EQ(v, 16);
}

TEST(ParseScore, RejectsWhatRedisRejects) {
  double v;
  for (std::string_view s : {"", "nan", "abc", " 1", "1 ", "1.5e", "1x"}) {
    EXPECT_FALSE(parse_score(s, &v)) << "\"" << s << "\"";
  }
}

// ---- ZSetValue ----

TEST(ZSetValue, SetAndRemoveKeepBothStructuresInStep) {
  ZSetValue z;
  EXPECT_TRUE(z.set("a", 1));
  EXPECT_TRUE(z.set("b", 2));
  EXPECT_FALSE(z.set("a", 3));
  EXPECT_EQ(z.order.size(), 2u);
  EXPECT_EQ(*z.scores.find("a"), 3);
  EXPECT_EQ(z.order.last()->member, "a");
  EXPECT_TRUE(z.remove("a"));
  EXPECT_FALSE(z.remove("a"));
  EXPECT_EQ(z.order.size(), 1u);
  EXPECT_EQ(z.scores.size(), 1u);
}

// ---- commands ----

class ZSets : public ::testing::Test {
 protected:
  ZSets() {
    register_keyspace_commands(table, ks);
    register_zset_commands(table, ks);
  }
  FakeClock clock;
  Keyspace ks{clock};
  CommandTable table;
};

TEST_F(ZSets, ScoreParsing) {
  play(table, {
      {{"ZADD", "z", "nan", "a"}, "-ERR value is not a valid float\r\n"},
      {{"ZADD", "z", "abc", "a"}, "-ERR value is not a valid float\r\n"},
      {{"ZADD", "z", "1", "a", "2"}, "-ERR syntax error\r\n"},
      {{"ZADD", "z"}, "-ERR wrong number of arguments for 'zadd' command\r\n"},
      {{"ZADD", "z", "+inf", "a"}, ":1\r\n"},
      {{"ZSCORE", "z", "a"}, "$3\r\ninf\r\n"},
      {{"ZADD", "z", " 1", "b"}, "-ERR value is not a valid float\r\n"},
      {{"ZADD", "z", "1.5e", "b"}, "-ERR value is not a valid float\r\n"},
      {{"ZADD", "z", "0x10", "b"}, ":1\r\n"},
      {{"ZINCRBY", "z", "1", "nope"}, "$1\r\n1\r\n"},
      {{"ZADD", "z", "inf", "c"}, ":1\r\n"},
      {{"ZINCRBY", "z", "-inf", "c"}, "-ERR resulting score is not a number (NaN)\r\n"},
  });
}

TEST_F(ZSets, ZaddOptions) {
  play(table, {
      {{"ZADD", "z", "1", "a", "2", "b"}, ":2\r\n"},
      {{"ZADD", "z", "NX", "10", "a", "3", "c"}, ":1\r\n"},
      {{"ZSCORE", "z", "a"}, "$1\r\n1\r\n"},
      {{"ZADD", "z", "XX", "10", "a", "4", "d"}, ":0\r\n"},
      {{"ZSCORE", "z", "a"}, "$2\r\n10\r\n"},
      {{"ZSCORE", "z", "d"}, "$-1\r\n"},
      {{"ZADD", "z", "CH", "11", "a", "2", "b", "5", "e"}, ":2\r\n"},
      {{"ZADD", "z", "GT", "5", "a"}, ":0\r\n"},
      {{"ZADD", "z", "GT", "CH", "20", "a"}, ":1\r\n"},
      {{"ZADD", "z", "LT", "CH", "30", "a", "1", "b"}, ":1\r\n"},
      {{"ZADD", "z", "NX", "XX", "1", "a"}, "-ERR XX and NX options at the same time are not compatible\r\n"},
      {{"ZADD", "z", "GT", "LT", "1", "a"}, "-ERR GT, LT, and/or NX options at the same time are not compatible\r\n"},
      {{"ZADD", "z", "NX", "GT", "1", "a"}, "-ERR GT, LT, and/or NX options at the same time are not compatible\r\n"},
      {{"ZADD", "z", "INCR", "5", "a"}, "$2\r\n25\r\n"},
      {{"ZADD", "z", "INCR", "5", "a", "1", "b"}, "-ERR INCR option supports a single increment-element pair\r\n"},
      {{"ZADD", "z", "NX", "INCR", "1", "a"}, "$-1\r\n"},
      {{"ZADD", "z", "XX", "INCR", "1", "nope"}, "$-1\r\n"},
      {{"ZINCRBY", "z", "2.5", "a"}, "$4\r\n27.5\r\n"},
      {{"ZINCRBY", "z", "1", "new"}, "$1\r\n1\r\n"},
      {{"ZCARD", "z"}, ":5\r\n"},
  });
}

TEST_F(ZSets, RangeByIndex) {
  play(table, {
      {{"ZADD", "z", "1", "a", "2", "b", "3", "c", "4", "d"}, ":4\r\n"},
      {{"ZRANGE", "z", "0", "-1"}, "*4\r\n$1\r\na\r\n$1\r\nb\r\n$1\r\nc\r\n$1\r\nd\r\n"},
      {{"ZRANGE", "z", "1", "2", "WITHSCORES"}, "*4\r\n$1\r\nb\r\n$1\r\n2\r\n$1\r\nc\r\n$1\r\n3\r\n"},
      {{"ZRANGE", "z", "-2", "-1"}, "*2\r\n$1\r\nc\r\n$1\r\nd\r\n"},
      {{"ZRANGE", "z", "0", "-1", "REV"}, "*4\r\n$1\r\nd\r\n$1\r\nc\r\n$1\r\nb\r\n$1\r\na\r\n"},
      {{"ZRANGE", "z", "0", "0", "REV", "WITHSCORES"}, "*2\r\n$1\r\nd\r\n$1\r\n4\r\n"},
      {{"ZRANGE", "z", "5", "10"}, "*0\r\n"},
      {{"ZRANGE", "z", "0", "-1", "LIMIT", "0", "1"}, "-ERR syntax error, LIMIT is only supported in combination with either BYSCORE or BYLEX\r\n"},
      {{"ZRANGE", "missing", "0", "-1"}, "*0\r\n"},
      {{"ZRANGE", "z", "a", "b"}, "-ERR value is not an integer or out of range\r\n"},
  });
}

TEST_F(ZSets, RangeByScore) {
  play(table, {
      {{"ZADD", "z", "1", "a", "2", "b", "3", "c", "4", "d", "5", "e"}, ":5\r\n"},
      {{"ZRANGE", "z", "2", "4", "BYSCORE"}, "*3\r\n$1\r\nb\r\n$1\r\nc\r\n$1\r\nd\r\n"},
      {{"ZRANGE", "z", "(2", "4", "BYSCORE"}, "*2\r\n$1\r\nc\r\n$1\r\nd\r\n"},
      {{"ZRANGE", "z", "2", "(4", "BYSCORE", "WITHSCORES"}, "*4\r\n$1\r\nb\r\n$1\r\n2\r\n$1\r\nc\r\n$1\r\n3\r\n"},
      {{"ZRANGE", "z", "-inf", "+inf", "BYSCORE", "LIMIT", "1", "2"}, "*2\r\n$1\r\nb\r\n$1\r\nc\r\n"},
      {{"ZRANGE", "z", "4", "2", "BYSCORE", "REV"}, "*3\r\n$1\r\nd\r\n$1\r\nc\r\n$1\r\nb\r\n"},
      {{"ZRANGE", "z", "+inf", "-inf", "BYSCORE", "REV", "LIMIT", "0", "2"}, "*2\r\n$1\r\ne\r\n$1\r\nd\r\n"},
      {{"ZRANGE", "z", "2", "4", "BYSCORE", "REV"}, "*0\r\n"},
      {{"ZRANGE", "z", "x", "4", "BYSCORE"}, "-ERR min or max is not a float\r\n"},
      {{"ZRANGE", "z", "(x", "4", "BYSCORE"}, "-ERR min or max is not a float\r\n"},
      {{"ZRANGE", "z", "1", "2", "BYSCORE", "LIMIT", "0"}, "-ERR syntax error\r\n"},
      {{"ZRANGE", "z", "1", "5", "BYSCORE", "LIMIT", "0", "-1"}, "*5\r\n$1\r\na\r\n$1\r\nb\r\n$1\r\nc\r\n$1\r\nd\r\n$1\r\ne\r\n"},
      {{"ZRANGE", "z", "1", "5", "BYSCORE", "LIMIT", "-1", "2"}, "*0\r\n"},
      {{"ZRANGEBYSCORE", "z", "2", "4"}, "*3\r\n$1\r\nb\r\n$1\r\nc\r\n$1\r\nd\r\n"},
      {{"ZRANGEBYSCORE", "z", "(1", "+inf", "WITHSCORES", "LIMIT", "0", "2"}, "*4\r\n$1\r\nb\r\n$1\r\n2\r\n$1\r\nc\r\n$1\r\n3\r\n"},
      {{"ZCOUNT", "z", "2", "4"}, ":3\r\n"},
      {{"ZCOUNT", "z", "(2", "(4"}, ":1\r\n"},
      {{"ZCOUNT", "z", "-inf", "+inf"}, ":5\r\n"},
      {{"ZCOUNT", "z", "10", "20"}, ":0\r\n"},
      {{"ZCOUNT", "z", "x", "1"}, "-ERR min or max is not a float\r\n"},
  });
}

TEST_F(ZSets, RankAndRem) {
  play(table, {
      {{"ZADD", "z", "10", "a", "20", "b", "30", "c"}, ":3\r\n"},
      {{"ZRANK", "z", "a"}, ":0\r\n"},
      {{"ZRANK", "z", "c"}, ":2\r\n"},
      {{"ZRANK", "z", "nope"}, "$-1\r\n"},
      {{"ZREVRANK", "z", "a"}, ":2\r\n"},
      {{"ZRANK", "z", "b", "WITHSCORE"}, "*2\r\n:1\r\n$2\r\n20\r\n"},
      {{"ZREVRANK", "z", "b", "WITHSCORE"}, "*2\r\n:1\r\n$2\r\n20\r\n"},
      {{"ZRANK", "missing", "a"}, "$-1\r\n"},
      {{"ZREM", "z", "a", "nope"}, ":1\r\n"},
      {{"ZREM", "z", "b", "c"}, ":2\r\n"},
      {{"EXISTS", "z"}, ":0\r\n"},
      {{"ZMSCORE", "z", "a"}, "*1\r\n$-1\r\n"},
      {{"ZADD", "z", "1", "a"}, ":1\r\n"},
      {{"ZMSCORE", "z", "a", "nope"}, "*2\r\n$1\r\n1\r\n$-1\r\n"},
  });
}

TEST_F(ZSets, EqualScoresOrderByMember) {
  play(table, {
      {{"ZADD", "z", "1", "b", "1", "c", "1", "a", "0", "z"}, ":4\r\n"},
      {{"ZRANGE", "z", "0", "-1"}, "*4\r\n$1\r\nz\r\n$1\r\na\r\n$1\r\nb\r\n$1\r\nc\r\n"},
      {{"ZRANK", "z", "c"}, ":3\r\n"},
  });
}

TEST_F(ZSets, WrongType) {
  play(table, {
      {{"SET", "s", "v"}, "+OK\r\n"},
      {{"ZADD", "s", "1", "a"}, "-WRONGTYPE Operation against a key holding the wrong kind of value\r\n"},
      {{"ZSCORE", "s", "a"}, "-WRONGTYPE Operation against a key holding the wrong kind of value\r\n"},
      {{"ZRANGE", "s", "0", "-1"}, "-WRONGTYPE Operation against a key holding the wrong kind of value\r\n"},
      {{"ZADD", "z", "1", "a"}, ":1\r\n"},
      {{"GET", "z"}, "-WRONGTYPE Operation against a key holding the wrong kind of value\r\n"},
      {{"TYPE", "z"}, "+zset\r\n"},
  });
}

// A leaderboard: 10 000 players, many score changes, ranks still right.
TEST_F(ZSets, Leaderboard) {
  std::string r;
  for (int i = 0; i < 10000; ++i) {
    table.dispatch(std::vector<std::string>{"ZADD", "lb", std::to_string(i), "p" + std::to_string(i)}, r);
  }
  for (int i = 0; i < 10000; i += 2) {
    table.dispatch(std::vector<std::string>{"ZINCRBY", "lb", "100000", "p" + std::to_string(i)}, r);
  }
  play(table, {
      {{"ZCARD", "lb"}, ":10000\r\n"},
      {{"ZREVRANK", "lb", "p9998"}, ":0\r\n"},
      {{"ZRANK", "lb", "p1"}, ":0\r\n"},
      {{"ZRANGE", "lb", "0", "1", "REV", "WITHSCORES"},
       "*4\r\n$5\r\np9998\r\n$6\r\n109998\r\n$5\r\np9996\r\n$6\r\n109996\r\n"},
      {{"ZCOUNT", "lb", "100000", "+inf"}, ":5000\r\n"},
  });
}

}  // namespace
}  // namespace rd
