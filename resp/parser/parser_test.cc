#include "resp/parser/parser.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <limits>
#include <string>
#include <vector>

#include "common/rng.h"

// Error messages, and which inputs are errors at all, were recorded from Redis 8.10 by
// `python3 tools/redis_reference.py tools/cases/parser.txt` (raw bytes sent to a real
// server) and, for parse_int64, tools/cases/strings.txt (INCR on these strings).

namespace rd::resp {
namespace {

using Args = std::vector<std::string>;

// The test's own encoder, so these tests don't depend on resp/encoder.
std::string req(const Args& args) {
  std::string out = "*" + std::to_string(args.size()) + "\r\n";
  for (const auto& a : args) out += "$" + std::to_string(a.size()) + "\r\n" + a + "\r\n";
  return out;
}

std::string status_name(ParseStatus s) {
  switch (s) {
    case ParseStatus::kOk: return "kOk";
    case ParseStatus::kIncomplete: return "kIncomplete";
    case ParseStatus::kError: return "kError";
  }
  return "?";
}

void expect_ok(std::string_view in, const Args& args, size_t consumed) {
  ParseResult r = parse_request(in);
  ASSERT_EQ(status_name(r.status), "kOk") << "error: " << r.error;
  EXPECT_EQ(r.args, args);
  EXPECT_EQ(r.consumed, consumed);
}

void expect_incomplete(std::string_view in) {
  ParseResult r = parse_request(in);
  EXPECT_EQ(status_name(r.status), "kIncomplete") << "error: " << r.error;
}

void expect_error(std::string_view in, std::string_view error) {
  ParseResult r = parse_request(in);
  ASSERT_EQ(status_name(r.status), "kError");
  EXPECT_EQ(r.error, error);
}

// ---- parse_int64 ----

int64_t must_parse(std::string_view s) {
  int64_t v = 12345;
  EXPECT_TRUE(parse_int64(s, &v)) << "\"" << s << "\"";
  return v;
}

TEST(ParseInt64, PlainNumbers) {
  EXPECT_EQ(must_parse("0"), 0);
  EXPECT_EQ(must_parse("7"), 7);
  EXPECT_EQ(must_parse("-7"), -7);
  EXPECT_EQ(must_parse("1234567890"), 1234567890);
}

TEST(ParseInt64, Extremes) {
  EXPECT_EQ(must_parse("9223372036854775807"), std::numeric_limits<int64_t>::max());
  EXPECT_EQ(must_parse("-9223372036854775808"), std::numeric_limits<int64_t>::min());
}

// Redis: INCR replies "ERR value is not an integer or out of range" for all of these.
TEST(ParseInt64, RejectsWhatRedisRejects) {
  for (std::string_view s : {"", "-", "abc", " 1", "1 ", "01", "00", "+1", "-0", "1.5", "1.0",
                             "1e3", "0x10", "9223372036854775808", "-9223372036854775809",
                             "99999999999999999999", "123456789012345678901234"}) {
    int64_t v = 12345;
    EXPECT_FALSE(parse_int64(s, &v)) << "\"" << s << "\"";
    EXPECT_EQ(v, 12345) << "*out must stay untouched on failure; input \"" << s << "\"";
  }
}

// The pitfall: overflow must be caught before it happens, not detected afterwards.
TEST(ParseInt64, OverflowJustPastTheLimitsIsRejected) {
  int64_t v;
  EXPECT_FALSE(parse_int64("9223372036854775810", &v));
  EXPECT_FALSE(parse_int64("18446744073709551615", &v));   // UINT64_MAX
  EXPECT_FALSE(parse_int64("18446744073709551616", &v));   // UINT64_MAX + 1 wraps to 0
  EXPECT_FALSE(parse_int64("-18446744073709551616", &v));
}

TEST(ParseInt64, RoundTripsStdToString) {
  Rng rng(7);
  for (int i = 0; i < 20000; ++i) {
    auto v = static_cast<int64_t>(rng.next() >> rng.below(64));
    if (rng.coin()) v = -v;
    std::string s = std::to_string(v);
    int64_t got = 0;
    ASSERT_TRUE(parse_int64(s, &got)) << s;
    ASSERT_EQ(got, v) << s;
  }
}

// ---- parse_request: valid input ----

TEST(Parser, SingleCommand) {
  expect_ok("*1\r\n$4\r\nPING\r\n", {"PING"}, 14);
}

TEST(Parser, CommandWithArguments) {
  expect_ok("*3\r\n$3\r\nSET\r\n$1\r\nk\r\n$5\r\nhello\r\n", {"SET", "k", "hello"}, 31);
}

TEST(Parser, EmptyInputIsIncomplete) {
  expect_incomplete("");
}

// Pipelining: a client sends several requests without waiting. Parse one at a time.
TEST(Parser, ParsesOnlyTheFirstOfPipelinedRequests) {
  std::string a = req({"PING"});
  std::string b = req({"ECHO", "a\r\nb"});
  expect_ok(a + b, {"PING"}, a.size());
  expect_ok(b, {"ECHO", "a\r\nb"}, b.size());
}

// Redis: ECHO of "a\r\nb" replies "$4\r\na\r\nb\r\n". The parser must use the length,
// never search for "\r\n" inside a payload.
TEST(Parser, PayloadMayContainCrlf) {
  expect_ok("*2\r\n$4\r\nECHO\r\n$4\r\na\r\nb\r\n", {"ECHO", "a\r\nb"}, 24);
}

TEST(Parser, PayloadMayContainZeroBytes) {
  std::string in("*2\r\n$4\r\nECHO\r\n$3\r\na\0b\r\n", 23);
  expect_ok(in, {"ECHO", std::string("a\0b", 3)}, 23);
}

TEST(Parser, EmptyArgument) {
  expect_ok("*2\r\n$4\r\nECHO\r\n$0\r\n\r\n", {"ECHO", ""}, 20);
}

// Redis: "*0\r\n", "*-1\r\n" and "*-5\r\n" are read and ignored; a PING after them is
// answered normally.
TEST(Parser, ZeroOrNegativeCountIsAnEmptyRequest) {
  expect_ok("*0\r\n", {}, 4);
  expect_ok("*-1\r\n*1\r\n$4\r\nPING\r\n", {}, 5);
  expect_ok("*-5\r\n", {}, 5);
}

// Redis: "*1\r\n$4\r\nPINGxy*1\r\n..." is answered with two PONGs. The two bytes after a
// payload are skipped without being looked at.
TEST(Parser, BytesAfterPayloadAreNotChecked) {
  expect_ok("*1\r\n$4\r\nPINGxy*1\r\n$4\r\nPING\r\n", {"PING"}, 14);
}

// The pitfall: data arrives in pieces of any size. Every proper prefix of a valid
// request is incomplete, never an error and never a shorter request.
TEST(Parser, EveryPrefixIsIncomplete) {
  std::string in = req({"SET", "key", "a\r\nvalue with \r\n inside", ""});
  for (size_t n = 0; n < in.size(); ++n) {
    ParseResult r = parse_request(std::string_view(in).substr(0, n));
    ASSERT_EQ(status_name(r.status), "kIncomplete")
        << "prefix of length " << n << ": \"" << in.substr(0, n) << "\"\nerror: " << r.error;
  }
  expect_ok(in, {"SET", "key", "a\r\nvalue with \r\n inside", ""}, in.size());
}

// The pitfall: counts come from the network. Redis accepts any count up to INT_MAX and
// waits; reserving 2^31 strings up front would ask for ~64 GB.
TEST(Parser, HugeCountDoesNotPreallocate) {
  expect_incomplete("*2147483647\r\n$4\r\nPING\r\n");  // Redis: no reply, waits
}

TEST(Parser, MaxBulkLengthIsAllowed) {
  expect_incomplete("*1\r\n$536870912\r\n");  // Redis: no reply, waits for 512 MB
}

// A server feeds what it reads from a socket, a few bytes at a time, into a buffer;
// parses as many requests as are complete; and drops the bytes it consumed.
TEST(Parser, RandomRequestsInRandomChunks) {
  Rng rng(2024);
  std::vector<Args> sent;
  std::string stream;
  for (int i = 0; i < 300; ++i) {
    Args args;
    size_t argc = 1 + rng.below(5);
    for (size_t j = 0; j < argc; ++j) {
      std::string a(rng.below(rng.coin(0.1) ? 3000 : 40), '\0');
      for (char& c : a) c = static_cast<char>(rng.coin(0.3) ? "\r\n$*0"[rng.below(5)] : rng.below(256));
      args.push_back(std::move(a));
    }
    stream += req(args);
    sent.push_back(std::move(args));
  }

  std::vector<Args> got;
  std::string buffer;
  size_t pos = 0;
  while (pos < stream.size()) {
    size_t n = std::min<size_t>(1 + rng.below(rng.coin(0.5) ? 8 : 512), stream.size() - pos);
    buffer.append(stream, pos, n);
    pos += n;
    for (;;) {
      ParseResult r = parse_request(buffer);
      ASSERT_NE(status_name(r.status), "kError") << r.error << " after request #" << got.size();
      if (r.status == ParseStatus::kIncomplete) break;
      ASSERT_GT(r.consumed, 0u);
      ASSERT_LE(r.consumed, buffer.size());
      got.push_back(std::move(r.args));
      buffer.erase(0, r.consumed);
    }
  }
  EXPECT_TRUE(buffer.empty()) << buffer.size() << " bytes left over";
  ASSERT_EQ(got.size(), sent.size());
  for (size_t i = 0; i < sent.size(); ++i) ASSERT_EQ(got[i], sent[i]) << "request #" << i;
}

// ---- parse_request: errors (all recorded from Redis) ----

TEST(ParserErrors, CountMustBeAnInteger) {
  for (std::string_view in : {"*abc\r\n", "*01\r\n", "*+1\r\n", "* 1\r\n"}) {
    expect_error(in, "ERR Protocol error: invalid multibulk length");
  }
}

TEST(ParserErrors, CountAboveIntMax) {
  expect_error("*2147483648\r\n", "ERR Protocol error: invalid multibulk length");
}

TEST(ParserErrors, ElementMustBeABulkString) {
  expect_error("*1\r\n:5\r\n", "ERR Protocol error: expected '$', got ':'");
  expect_error("*1\r\n+PING\r\n", "ERR Protocol error: expected '$', got '+'");
}

TEST(ParserErrors, ErrorInALaterElement) {
  expect_error("*2\r\n$4\r\nECHO\r\n:5\r\n", "ERR Protocol error: expected '$', got ':'");
}

TEST(ParserErrors, BulkLengthMustBeANonNegativeInteger) {
  for (std::string_view in : {"*1\r\n$-1\r\n", "*1\r\n$abc\r\n", "*1\r\n$04\r\nPING\r\n",
                              "*1\r\n$99999999999999999999\r\n"}) {
    expect_error(in, "ERR Protocol error: invalid bulk length");
  }
}

// The pitfall: reject a length as soon as its line is complete, without waiting for
// (or allocating) the payload.
TEST(ParserErrors, BulkLengthAbove512MbIsRejectedImmediately) {
  expect_error("*1\r\n$536870913\r\n", "ERR Protocol error: invalid bulk length");
}

// The pitfall: garbage without a newline must not be buffered forever. Redis gives up
// once more than 64 KB of a count line has arrived.
TEST(ParserErrors, CountLineLongerThan64KbIsAnError) {
  expect_incomplete("*" + std::string(65535, '1'));  // 65536 bytes: Redis waits
  expect_error("*" + std::string(65536, '1'),       // 65537 bytes
               "ERR Protocol error: too big mbulk count string");
}

// ...but Redis 8 has no such limit for the "$<len>" line: it keeps waiting.
TEST(ParserErrors, LongBulkLengthLineIsStillIncomplete) {
  expect_incomplete("*1\r\n$" + std::string(300000, '1'));
}

}  // namespace
}  // namespace rd::resp
