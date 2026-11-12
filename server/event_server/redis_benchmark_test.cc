// redis-benchmark (the real one) against your server.
#include <gtest/gtest.h>

#include <cstdio>
#include <string>
#include <thread>
#include <unistd.h>

#include "db/strings/strings.h"
#include "server/event_server/event_server.h"

namespace rd {
namespace {

std::string find_tool(const char* name) {
  for (const char* dir : {"/opt/homebrew/bin/", "/usr/local/bin/", "/usr/bin/"}) {
    std::string p = std::string(dir) + name;
    if (::access(p.c_str(), X_OK) == 0) return p;
  }
  return "";
}

TEST(RedisBenchmark, RunsClean) {
  std::string bench = find_tool("redis-benchmark");
  if (bench.empty()) GTEST_SKIP() << "redis-benchmark not installed (brew install redis)";
  SystemClock clock;
  StringDb db;
  CommandTable table;
  register_connection_commands(table);
  register_string_commands(table, db);
  EventServer server(table, clock);
  uint16_t port = server.listen("127.0.0.1", 0);
  ASSERT_NE(port, 0);
  std::thread t([&] { server.run(); });
  std::string cmd = bench + " -p " + std::to_string(port) +
                    " -c 50 -n 50000 -P 16 -t ping_mbulk,set,get,incr --csv 2>&1";
  std::string out;
  FILE* f = ::popen(cmd.c_str(), "r");
  char buf[4096];
  size_t n;
  while ((n = std::fread(buf, 1, sizeof buf, f)) > 0) out.append(buf, n);
  int status = ::pclose(f);
  server.stop();
  t.join();
  std::printf("%s", out.c_str());
  EXPECT_EQ(status, 0);
  EXPECT_EQ(out.find("ERR"), std::string::npos) << out;
  EXPECT_NE(out.find("\"SET\""), std::string::npos) << out;
}

}  // namespace
}  // namespace rd
