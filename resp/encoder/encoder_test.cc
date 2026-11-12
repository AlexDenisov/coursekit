#include "resp/encoder/encoder.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <limits>
#include <string>

#include "common/rng.h"

// Expected bytes marked "Redis:" were recorded from Redis 8.10 by
// `python3 tools/redis_reference.py tools/cases/encoder.txt`.

namespace rd::resp {
namespace {

std::string fmt(int64_t v) {
  char buf[kMaxInt64Chars];
  size_t n = format_int64(v, buf);
  EXPECT_LE(n, kMaxInt64Chars);
  return std::string(buf, n);
}

TEST(FormatInt64, SmallValues) {
  EXPECT_EQ(fmt(0), "0");
  EXPECT_EQ(fmt(7), "7");
  EXPECT_EQ(fmt(-7), "-7");
  EXPECT_EQ(fmt(10), "10");
  EXPECT_EQ(fmt(-100), "-100");
  EXPECT_EQ(fmt(1234567890), "1234567890");
}

TEST(FormatInt64, Int64MaxHasNineteenDigits) {
  EXPECT_EQ(fmt(std::numeric_limits<int64_t>::max()), "9223372036854775807");
}

// The pitfall: -INT64_MIN doesn't fit in an int64_t.
TEST(FormatInt64, Int64MinDoesNotOverflow) {
  EXPECT_EQ(fmt(std::numeric_limits<int64_t>::min()), "-9223372036854775808");
}

TEST(FormatInt64, PowersOfTenAndTheirNeighbours) {
  int64_t p = 1;
  for (int i = 0; i < 19; ++i, p *= 10) {
    for (int64_t v : {p - 1, p, p + 1, -p + 1, -p, -p - 1}) {
      EXPECT_EQ(fmt(v), std::to_string(v)) << "v = " << v;
    }
  }
}

// The oracle: std::to_string.
TEST(FormatInt64, MatchesStdToStringOnRandomValues) {
  Rng rng(42);
  for (int i = 0; i < 20000; ++i) {
    // Random magnitudes, not just random bits: most random 64-bit values have 19 digits.
    int bits = static_cast<int>(rng.below(64));
    auto v = static_cast<int64_t>(rng.next() >> bits);
    if (rng.coin()) v = -v;
    ASSERT_EQ(fmt(v), std::to_string(v)) << "v = " << v;
  }
}

TEST(Encoder, SimpleString) {
  std::string out;
  append_simple_string(out, "PONG");
  EXPECT_EQ(out, "+PONG\r\n");  // Redis: PING
}

TEST(Encoder, EmptySimpleString) {
  std::string out;
  append_simple_string(out, "");
  EXPECT_EQ(out, "+\r\n");
}

TEST(Encoder, Error) {
  std::string out;
  append_error(out, "ERR wrong number of arguments for 'get' command");
  EXPECT_EQ(out, "-ERR wrong number of arguments for 'get' command\r\n");  // Redis: GET
}

// The pitfall: an error that quotes user input (here a command named "foo\r\nbar1")
// would otherwise end the reply early and inject a second one.
TEST(Encoder, ErrorNewlinesBecomeSpaces) {
  std::string out;
  append_error(out, "ERR unknown command 'foo\r\nbar1'");
  EXPECT_EQ(out, "-ERR unknown command 'foo  bar1'\r\n");  // Redis
}

TEST(Encoder, Integers) {
  std::string out;
  append_integer(out, 1);
  EXPECT_EQ(out, ":1\r\n");  // Redis: INCR n
  out.clear();
  append_integer(out, 0);
  EXPECT_EQ(out, ":0\r\n");
  out.clear();
  append_integer(out, std::numeric_limits<int64_t>::min());
  EXPECT_EQ(out, ":-9223372036854775808\r\n");  // Redis: DECR of -9223372036854775807
  out.clear();
  append_integer(out, std::numeric_limits<int64_t>::max());
  EXPECT_EQ(out, ":9223372036854775807\r\n");  // Redis: INCR of 9223372036854775806
}

TEST(Encoder, BulkString) {
  std::string out;
  append_bulk_string(out, "hello");
  EXPECT_EQ(out, "$5\r\nhello\r\n");  // Redis: GET s
}

TEST(Encoder, EmptyBulkStringIsNotNull) {
  std::string out;
  append_bulk_string(out, "");
  EXPECT_EQ(out, "$0\r\n\r\n");  // Redis: GET of ""
}

// The length prefix is what makes bulk strings binary safe: the reader never looks
// for "\r\n" inside the payload.
TEST(Encoder, BulkStringMayContainCrlf) {
  std::string out;
  append_bulk_string(out, "a\r\nb");
  EXPECT_EQ(out, "$4\r\na\r\nb\r\n");  // Redis: GET crlf
}

TEST(Encoder, BulkStringMayContainZeroBytes) {
  std::string out;
  append_bulk_string(out, std::string_view("\0\1\r\n", 4));
  EXPECT_EQ(out, std::string("$4\r\n\0\1\r\n\r\n", 10));  // Redis: GET bin
}

TEST(Encoder, LongBulkString) {
  std::string payload(100'000, 'x');
  std::string out;
  append_bulk_string(out, payload);
  EXPECT_EQ(out, "$100000\r\n" + payload + "\r\n");
}

TEST(Encoder, NullBulk) {
  std::string out;
  append_null_bulk(out);
  EXPECT_EQ(out, "$-1\r\n");  // Redis: GET missing
}

TEST(Encoder, ArrayOfBulkStrings) {
  std::string out;
  append_array_header(out, 2);
  append_bulk_string(out, "a");
  append_bulk_string(out, "bb");
  EXPECT_EQ(out, "*2\r\n$1\r\na\r\n$2\r\nbb\r\n");  // Redis: LRANGE l 0 -1
}

TEST(Encoder, EmptyArray) {
  std::string out;
  append_array_header(out, 0);
  EXPECT_EQ(out, "*0\r\n");  // Redis: LRANGE nolist 0 -1
}

TEST(Encoder, ArrayMayHoldNulls) {
  std::string out;
  append_array_header(out, 2);
  append_bulk_string(out, "hello");
  append_null_bulk(out);
  EXPECT_EQ(out, "*2\r\n$5\r\nhello\r\n$-1\r\n");  // Redis: MGET s missing
}

TEST(Encoder, NullArray) {
  std::string out;
  append_null_array(out);
  EXPECT_EQ(out, "*-1\r\n");  // Redis: BLPOP nolist 0.01
}

TEST(Encoder, AppendsWithoutClearing) {
  std::string out = "+OK\r\n";
  append_integer(out, 42);
  append_simple_string(out, "PONG");
  append_bulk_string(out, "x");
  EXPECT_EQ(out, "+OK\r\n:42\r\n+PONG\r\n$1\r\nx\r\n");
}

}  // namespace
}  // namespace rd::resp
