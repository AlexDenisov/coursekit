#include "cmd/dispatch/dispatch.h"

#include <gtest/gtest.h>

#include <string>
#include <vector>

// Expected replies marked "Redis:" were recorded from Redis 8.10 by
// `python3 tools/redis_reference.py tools/cases/dispatch.txt`. The tests write the
// expected bytes out in full rather than calling resp/encoder, so a bug there can't
// hide one here.

namespace rd {
namespace {

using Args = std::vector<std::string>;

std::string run(const CommandTable& table, const Args& args) {
  std::string reply;
  table.dispatch(args, reply);
  return reply;
}

CommandTable connection_table() {
  CommandTable t;
  register_connection_commands(t);
  return t;
}

// ---- lookup ----

TEST(CommandTable, FindIgnoresCase) {
  CommandTable t;
  t.add({"get", 2, [](CommandContext&) {}});
  ASSERT_NE(t.find("get"), nullptr);
  EXPECT_EQ(t.find("get")->name, "get");
  EXPECT_EQ(t.find("GET"), t.find("get"));
  EXPECT_EQ(t.find("gEt"), t.find("get"));
}

TEST(CommandTable, FindNeedsTheWholeName) {
  CommandTable t;
  t.add({"get", 2, [](CommandContext&) {}});
  EXPECT_EQ(t.find("ge"), nullptr);
  EXPECT_EQ(t.find("gets"), nullptr);
  EXPECT_EQ(t.find(""), nullptr);
  EXPECT_EQ(t.find(std::string_view("get\0", 4)), nullptr);
}

// ---- dispatch ----

TEST(CommandTable, CallsTheCommandWithArgsAsSent) {
  CommandTable t;
  Args seen;
  t.add({"set", -3, [&](CommandContext& c) {
           seen.assign(c.args.begin(), c.args.end());
           c.reply += "+OK\r\n";
         }});
  EXPECT_EQ(run(t, {"SeT", "k", "v"}), "+OK\r\n");
  EXPECT_EQ(seen, (Args{"SeT", "k", "v"}));
}

TEST(CommandTable, AppendsToTheReplyBuffer) {
  CommandTable t = connection_table();
  std::string reply = "+OK\r\n";
  t.dispatch(Args{"PING"}, reply);
  t.dispatch(Args{"NOPE"}, reply);
  EXPECT_EQ(reply, "+OK\r\n+PONG\r\n-ERR unknown command 'NOPE'\r\n");
}

TEST(CommandTable, ExactArity) {
  CommandTable t;
  int calls = 0;
  t.add({"get", 2, [&](CommandContext& c) { ++calls; c.reply += "+OK\r\n"; }});
  EXPECT_EQ(run(t, {"GET"}), "-ERR wrong number of arguments for 'get' command\r\n");
  EXPECT_EQ(run(t, {"GET", "a", "b"}), "-ERR wrong number of arguments for 'get' command\r\n");
  EXPECT_EQ(calls, 0) << "a command must not run with the wrong number of arguments";
  EXPECT_EQ(run(t, {"GET", "a"}), "+OK\r\n");
  EXPECT_EQ(calls, 1);
}

TEST(CommandTable, MinimumArity) {
  CommandTable t;
  t.add({"del", -2, [](CommandContext& c) { c.reply += ":" + std::to_string(c.args.size()) + "\r\n"; }});
  EXPECT_EQ(run(t, {"DEL"}), "-ERR wrong number of arguments for 'del' command\r\n");
  EXPECT_EQ(run(t, {"DEL", "a"}), ":2\r\n");
  EXPECT_EQ(run(t, {"DEL", "a", "b", "c", "d"}), ":5\r\n");
}

// The error names the command as registered, not as typed.
TEST(CommandTable, ArityErrorUsesTheLowercaseName) {
  CommandTable t;
  t.add({"get", 2, [](CommandContext&) {}});
  EXPECT_EQ(run(t, {"GeT"}), "-ERR wrong number of arguments for 'get' command\r\n");
}

TEST(ArityError, Format) {
  EXPECT_EQ(arity_error("mset"), "ERR wrong number of arguments for 'mset' command");
}

// ---- unknown commands ----

TEST(UnknownCommand, NameOnly) {
  EXPECT_EQ(run(connection_table(), {"FOO"}), "-ERR unknown command 'FOO'\r\n");  // Redis
}

TEST(UnknownCommand, WithArgs) {
  CommandTable t = connection_table();
  EXPECT_EQ(run(t, {"FOO", "a"}),  // Redis
            "-ERR unknown command 'FOO', with args beginning with: 'a' \r\n");
  EXPECT_EQ(run(t, {"FOO", "a", "b", "c"}),  // Redis
            "-ERR unknown command 'FOO', with args beginning with: 'a' 'b' 'c' \r\n");
  EXPECT_EQ(run(t, {"foo", "x y"}),  // Redis
            "-ERR unknown command 'foo', with args beginning with: 'x y' \r\n");
}

// The pitfall: the message quotes user input. CR/LF become spaces in append_error.
TEST(UnknownCommand, NewlinesInArgsAreNeutralised) {
  EXPECT_EQ(run(connection_table(), {"NOPE", "a\r\nb"}),  // Redis
            "-ERR unknown command 'NOPE', with args beginning with: 'a  b' \r\n");
}

TEST(UnknownCommand, LongNameIsCutTo128Bytes) {
  EXPECT_EQ(run(connection_table(), {std::string(200, 'X')}),  // Redis
            "-ERR unknown command '" + std::string(128, 'X') + "'\r\n");
}

TEST(UnknownCommand, LongArgIsCutTo128Bytes) {
  EXPECT_EQ(run(connection_table(), {"FOO", std::string(200, 'a')}),  // Redis
            "-ERR unknown command 'FOO', with args beginning with: '" + std::string(128, 'a') +
                "' \r\n");
}

// The budget is shared: after 'a...a' (100 + 3 bytes) only 25 bytes of the next arg fit,
// and then the list is 131 bytes long, so 'c' is left out.
TEST(UnknownCommand, ArgsShareOneBudget) {
  EXPECT_EQ(run(connection_table(), {"FOO", std::string(100, 'a'), std::string(100, 'b'), "c"}),
            "-ERR unknown command 'FOO', with args beginning with: '" + std::string(100, 'a') +
                "' '" + std::string(25, 'b') + "' \r\n");  // Redis
}

// Ten args of 13 bytes each ("'abcdefghij' ") fit: 9 * 13 = 117 < 128 lets the tenth in.
TEST(UnknownCommand, ManyShortArgs) {
  Args args = {"FOO"};
  for (int i = 0; i < 20; ++i) args.push_back("abcdefghij");
  std::string listed;
  for (int i = 0; i < 10; ++i) listed += "'abcdefghij' ";
  EXPECT_EQ(run(connection_table(), args),  // Redis
            "-ERR unknown command 'FOO', with args beginning with: " + listed + "\r\n");
}

// ---- PING and ECHO ----

TEST(Ping, Pong) {
  CommandTable t = connection_table();
  EXPECT_EQ(run(t, {"PING"}), "+PONG\r\n");  // Redis
  EXPECT_EQ(run(t, {"ping"}), "+PONG\r\n");  // Redis
  EXPECT_EQ(run(t, {"PiNg"}), "+PONG\r\n");  // Redis
}

TEST(Ping, WithMessage) {
  EXPECT_EQ(run(connection_table(), {"PING", "hello"}), "$5\r\nhello\r\n");  // Redis
}

TEST(Ping, AtMostOneMessage) {
  EXPECT_EQ(run(connection_table(), {"PING", "a", "b"}),  // Redis
            "-ERR wrong number of arguments for 'ping' command\r\n");
}

TEST(Echo, Echoes) {
  CommandTable t = connection_table();
  EXPECT_EQ(run(t, {"ECHO", "hi"}), "$2\r\nhi\r\n");  // Redis
  EXPECT_EQ(run(t, {"ECHO", ""}), "$0\r\n\r\n");
  EXPECT_EQ(run(t, {"ECHO"}), "-ERR wrong number of arguments for 'echo' command\r\n");  // Redis
  EXPECT_EQ(run(t, {"ECHO", "a", "b"}),  // Redis
            "-ERR wrong number of arguments for 'echo' command\r\n");
}

TEST(Echo, BinarySafe) {
  EXPECT_EQ(run(connection_table(), {"ECHO", "a\r\nb"}), "$4\r\na\r\nb\r\n");
}

}  // namespace
}  // namespace rd
