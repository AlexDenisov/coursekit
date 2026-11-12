// The real thing: the redis-cli binary talking to your server.
#include <gtest/gtest.h>

#include <cstdio>
#include <string>
#include <thread>
#include <unistd.h>

#include "db/strings/strings.h"
#include "server/hello_cli/server.h"

namespace rd {
namespace {

std::string find_redis_cli() {
  for (const char* p : {"/opt/homebrew/bin/redis-cli", "/usr/local/bin/redis-cli", "/usr/bin/redis-cli"}) {
    if (::access(p, X_OK) == 0) return p;
  }
  return "";
}

std::string run(const std::string& cmd) {
  std::string out;
  FILE* f = ::popen(cmd.c_str(), "r");
  char buf[4096];
  size_t n;
  while ((n = std::fread(buf, 1, sizeof buf, f)) > 0) out.append(buf, n);
  ::pclose(f);
  return out;
}

TEST(RedisCli, Talks) {
  std::string cli = find_redis_cli();
  if (cli.empty()) GTEST_SKIP() << "redis-cli not installed (brew install redis)";
  StringDb db;
  CommandTable table;
  register_connection_commands(table);
  register_string_commands(table, db);
  BlockingServer server(table);
  uint16_t port = server.listen("127.0.0.1", 0);
  ASSERT_NE(port, 0);
  std::thread t([&] {
    for (int i = 0; i < 5; ++i) server.serve_one();
  });
  std::string base = cli + " -p " + std::to_string(port) + " ";
  EXPECT_EQ(run(base + "PING"), "PONG\n");
  EXPECT_EQ(run(base + "SET greeting 'hello world'"), "OK\n");
  EXPECT_EQ(run(base + "GET greeting"), "hello world\n");
  EXPECT_EQ(run(base + "INCRBY n 41"), "41\n");
  EXPECT_EQ(run(base + "INCR n"), "42\n");
  t.join();
}

}  // namespace
}  // namespace rd
