// redis-benchmark against your server and against the real thing, same workload.
#include <gtest/gtest.h>

#include <signal.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cstdio>
#include <map>
#include <sstream>
#include <string>
#include <thread>

#include "server/lean_redis/lean_redis.h"

extern char** environ;

namespace rd {
namespace {

std::string find_tool(const char* name) {
  for (const char* dir : {"/opt/homebrew/bin/", "/usr/local/bin/", "/usr/bin/"}) {
    std::string p = std::string(dir) + name;
    if (::access(p.c_str(), X_OK) == 0) return p;
  }
  return "";
}

// test name -> requests per second, from redis-benchmark --csv.
std::map<std::string, double> bench(const std::string& tool, uint16_t port) {
  std::string cmd = tool + " -p " + std::to_string(port) +
                    " -c 50 -n 200000 -P 16 -r 100000 -t set,get,incr,lpush,zadd --csv 2>/dev/null";
  std::string out;
  FILE* f = ::popen(cmd.c_str(), "r");
  char buf[4096];
  size_t n;
  while ((n = std::fread(buf, 1, sizeof buf, f)) > 0) out.append(buf, n);
  ::pclose(f);
  std::map<std::string, double> rps;
  std::istringstream lines(out);
  std::string line;
  while (std::getline(lines, line)) {
    if (line.size() < 3 || line[0] != '"' || line.rfind("\"test\"", 0) == 0) continue;
    size_t q = line.find('"', 1);
    std::string name = line.substr(1, q - 1);
    size_t a = line.find('"', q + 1), b = line.find('"', a + 1);
    rps[name] = std::stod(line.substr(a + 1, b - a - 1));
  }
  return rps;
}

TEST(VersusRedis, WithinTwoTimes) {
  std::string tool = find_tool("redis-benchmark"), server = find_tool("redis-server");
  if (tool.empty() || server.empty()) GTEST_SKIP() << "Redis not installed";

  SystemClock clock;
  LeanRedis ours(clock);
  uint16_t our_port = ours.listen("127.0.0.1", 0);
  ASSERT_NE(our_port, 0);
  std::thread t([&] { ours.run(); });
  auto mine = bench(tool, our_port);
  ours.stop();
  t.join();

  uint16_t their_port = our_port + 1;
  std::string port_s = std::to_string(their_port);
  const char* argv[] = {server.c_str(), "--port", port_s.c_str(), "--save", "", "--appendonly", "no", nullptr};
  pid_t pid;
  ASSERT_EQ(::posix_spawn(&pid, server.c_str(), nullptr, nullptr, const_cast<char**>(argv), environ), 0);
  std::this_thread::sleep_for(std::chrono::milliseconds(300));
  auto theirs = bench(tool, their_port);
  ::kill(pid, SIGTERM);
  ::waitpid(pid, nullptr, 0);

  ASSERT_FALSE(mine.empty());
  for (auto& [name, rps] : theirs) {
    double ratio = mine.count(name) ? mine[name] / rps : 0;
    std::printf("%-12s redis %10.0f/s   yours %10.0f/s   %.2fx\n", name.c_str(), rps, mine[name], ratio);
    EXPECT_GE(ratio, 0.5) << name << " is more than 2x slower than Redis";
  }
}

}  // namespace
}  // namespace rd
