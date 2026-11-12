#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "resp/parser/parser.h"

// Inline requests. Every expectation was recorded from Redis 8.10 by sending the raw
// line (`python3 tools/redis_reference.py tools/cases/parser.txt`, "inline" group).

namespace rd::resp {
namespace {

using Args = std::vector<std::string>;

void expect_ok(std::string_view in, const Args& args, size_t consumed) {
  for (ParseResult r : {parse_inline(in), parse_request(in)}) {
    ASSERT_EQ(r.status, ParseStatus::kOk) << "input \"" << in << "\", error: " << r.error;
    EXPECT_EQ(r.args, args) << "input \"" << in << "\"";
    EXPECT_EQ(r.consumed, consumed) << "input \"" << in << "\"";
  }
}

void expect_error(std::string_view in, std::string_view error) {
  for (ParseResult r : {parse_inline(in), parse_request(in)}) {
    ASSERT_EQ(r.status, ParseStatus::kError) << "input \"" << in << "\"";
    EXPECT_EQ(r.error, error) << "input \"" << in << "\"";
  }
}

TEST(Inline, CrlfOrJustLf) {
  expect_ok("PING\r\n", {"PING"}, 6);
  expect_ok("PING\n", {"PING"}, 5);
}

TEST(Inline, NoNewlineYetIsIncomplete) {
  EXPECT_EQ(parse_inline("PING").status, ParseStatus::kIncomplete);
  EXPECT_EQ(parse_inline("PING\r").status, ParseStatus::kIncomplete);
}

TEST(Inline, SplitsOnRunsOfSpaces) {
  expect_ok("ECHO   hello\r\n", {"ECHO", "hello"}, 14);
  expect_ok("ECHO\thello \r\n", {"ECHO", "hello"}, 13);
}

TEST(Inline, ParsesOnlyTheFirstLine) {
  expect_ok("ECHO hello\r\nPING\r\n", {"ECHO", "hello"}, 12);
}

TEST(Inline, DoubleQuotes) {
  expect_ok("ECHO \"hello world\"\r\n", {"ECHO", "hello world"}, 20);
}

TEST(Inline, EscapesInDoubleQuotes) {
  expect_ok("ECHO \"a\\x41\\n\"\r\n", {"ECHO", "aA\n"}, 16);
  expect_ok("ECHO \"tab\\there \\\"q\\\" \\\\\"\r\n", {"ECHO", "tab\there \"q\" \\"}, 27);
}

TEST(Inline, SingleQuotesOnlyEscapeTheQuote) {
  expect_ok("ECHO 'a\\'b'\r\n", {"ECHO", "a'b"}, 13);
  expect_ok("ECHO 'a\\nb'\r\n", {"ECHO", "a\\nb"}, 13);
}

TEST(Inline, EmptyQuotedArgument) {
  expect_ok("ECHO \"\"\r\n", {"ECHO", ""}, 9);
}

// Redis: a blank line is read and ignored; a PING after it is answered normally.
TEST(Inline, BlankLineIsAnEmptyRequest) {
  expect_ok("\r\nPING\r\n", {}, 2);
  expect_ok("  \r\nPING\r\n", {}, 4);
}

TEST(Inline, UnbalancedQuotes) {
  for (std::string_view in : {"ECHO \"abc\r\n", "ECHO 'it''s'\r\n", "ECHO \"abc\"def\r\n",
                              "ECHO 'abc\r\n"}) {
    expect_error(in, "ERR Protocol error: unbalanced quotes in request");
  }
}

TEST(Inline, LineLongerThan64KbIsAnError) {
  std::string waits = "PING" + std::string(65532, ' ');  // 65536 bytes: Redis waits
  EXPECT_EQ(parse_inline(waits).status, ParseStatus::kIncomplete);
  EXPECT_EQ(parse_request(waits).status, ParseStatus::kIncomplete);
  expect_error(waits + " ", "ERR Protocol error: too big inline request");
}

}  // namespace
}  // namespace rd::resp
