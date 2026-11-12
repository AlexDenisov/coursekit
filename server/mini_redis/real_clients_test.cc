// The real redis-cli and redis-benchmark against your mini Redis.
#include <gtest/gtest.h>

#include <cstdio>
#include <string>
#include <thread>
#include <unistd.h>

#include "server/mini_redis/mini_redis.h"

namespace rd {
namespace {

std::string find_tool(const char* name) {
  for (const char* dir : {"/opt/homebrew/bin/", "/usr/local/bin/", "/usr/bin/"}) {
    std::string p = std::string(dir) + name;
    if (::access(p.c_str(), X_OK) == 0) return p;
  }
  return "";
}

std::string run(const std::string& cmd, int* status = nullptr) {
  std::string out;
  FILE* f = ::popen((cmd + " 2>&1").c_str(), "r");
  char buf[4096];
  size_t n;
  while ((n = std::fread(buf, 1, sizeof buf, f)) > 0) out.append(buf, n);
  int s = ::pclose(f);
  if (status) *status = s;
  return out;
}

class RealClients : public ::testing::Test {
 protected:
  void SetUp() override {
    port_ = server_.listen("127.0.0.1", 0);
    ASSERT_NE(port_, 0);
    thread_ = std::thread([this] { server_.run(); });
  }
  void TearDown() override {
    server_.stop();
    thread_.join();
  }
  SystemClock clock_;
  MiniRedis server_{clock_};
  uint16_t port_ = 0;
  std::thread thread_;
};

TEST_F(RealClients, RedisCli) {
  std::string cli = find_tool("redis-cli");
  if (cli.empty()) GTEST_SKIP() << "redis-cli not installed";
  std::string base = cli + " -p " + std::to_string(port_) + " ";
  EXPECT_EQ(run(base + "RPUSH queue a b c"), "3\n");
  EXPECT_EQ(run(base + "LRANGE queue 0 -1"), "a\nb\nc\n");
  EXPECT_EQ(run(base + "HSET user:1 name ann"), "1\n");
  EXPECT_EQ(run(base + "HGET user:1 name"), "ann\n");
  EXPECT_EQ(run(base + "SET temp x EX 100"), "OK\n");
  EXPECT_EQ(run(base + "TTL temp"), "100\n");
  EXPECT_EQ(run(base + "TYPE user:1"), "hash\n");
  // --scan is built on SCAN.
  std::string keys = run(base + "--scan --pattern 'user:*'");
  EXPECT_EQ(keys, "user:1\n");
}

TEST_F(RealClients, RedisBenchmark) {
  std::string bench = find_tool("redis-benchmark");
  if (bench.empty()) GTEST_SKIP() << "redis-benchmark not installed";
  int status = 0;
  std::string out = run(bench + " -p " + std::to_string(port_) +
                            " -c 50 -n 50000 -P 16 -t ping_mbulk,set,get,incr,lpush,rpush,lpop,rpop,sadd,hset,spop,lrange_100 --csv",
                        &status);
  std::printf("%s", out.c_str());
  EXPECT_EQ(status, 0);
  EXPECT_EQ(out.find("ERR"), std::string::npos);
}

}  // namespace
}  // namespace rd
