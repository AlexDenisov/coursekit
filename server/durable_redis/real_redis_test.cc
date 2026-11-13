// The real thing, both ways: real Redis loads your dump.rdb; your server follows a real
// Redis primary.
#include <gtest/gtest.h>

#include <signal.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>

#include <chrono>
#include <cstdio>
#include <memory>
#include <string>
#include <thread>

#include "common/test_client.h"
#include "server/durable_redis/durable_redis.h"

extern char** environ;

namespace rd {
namespace {

std::string find_server() {
  for (const char* p : {"/opt/homebrew/bin/redis-server", "/usr/local/bin/redis-server", "/usr/bin/redis-server"}) {
    if (::access(p, X_OK) == 0) return p;
  }
  return "";
}

pid_t spawn_redis(const std::string& server, uint16_t port, const std::string& dir) {
  std::string port_s = std::to_string(port);
  const char* argv[] = {server.c_str(), "--port", port_s.c_str(), "--dir", dir.c_str(), "--save", "", "--appendonly",
                        "no", "--rdbcompression", "no", nullptr};
  pid_t pid;
  ::posix_spawn(&pid, server.c_str(), nullptr, nullptr, const_cast<char**>(argv), environ);
  return pid;
}

TEST(RealRedis, LoadsYourSnapshot) {
  std::string server = find_server();
  if (server.empty()) GTEST_SKIP() << "redis-server not installed";
  char dir[] = "/tmp/durrealXXXXXX";
  ::mkdtemp(dir);
  SystemClock clock;
  {
    DurableRedisOptions o;
    o.dir = dir;
    o.appendonly = false;
    DurableRedis ours(clock, o);
    ASSERT_TRUE(ours.load());
    uint16_t port = ours.listen("127.0.0.1", 0);
    std::thread t([&] { ours.run(); });
    {
      TestClient c(port);
      c.call({"SET", "greeting", "hello"});
      c.call({"ZADD", "board", "10", "ann"});
      c.call({"RPUSH", "queue", "a", "b"});
      c.call({"HSET", "user:1", "name", "ann"});
      EXPECT_EQ(c.call({"SAVE"}), "+OK\r\n");
    }
    ours.stop();
    t.join();
  }
  uint16_t port = static_cast<uint16_t>(22000 + ::getpid() % 20000);
  pid_t pid = spawn_redis(server, port, dir);
  {
    TestClient c(port);
    EXPECT_EQ(c.call({"GET", "greeting"}), "$5\r\nhello\r\n");
    EXPECT_EQ(c.call({"LRANGE", "queue", "0", "-1"}), "*2\r\n$1\r\na\r\n$1\r\nb\r\n");
    EXPECT_EQ(c.call({"HGET", "user:1", "name"}), "$3\r\nann\r\n");
  }
  ::kill(pid, SIGTERM);
  ::waitpid(pid, nullptr, 0);
}

TEST(RealRedis, YourServerFollowsARealPrimary) {
  std::string server = find_server();
  if (server.empty()) GTEST_SKIP() << "redis-server not installed";
  char pdir[] = "/tmp/durprimXXXXXX", rdir[] = "/tmp/durreplXXXXXX";
  ::mkdtemp(pdir);
  ::mkdtemp(rdir);
  uint16_t pport = static_cast<uint16_t>(23000 + ::getpid() % 20000);
  pid_t pid = spawn_redis(server, pport, pdir);
  TestClient primary(pport);
  primary.call({"SET", "before", "1"});

  SystemClock clock;
  DurableRedisOptions o;
  o.dir = rdir;
  DurableRedis ours(clock, o);
  ASSERT_TRUE(ours.load());
  uint16_t port = ours.listen("127.0.0.1", 0);
  std::thread t([&] { ours.run(); });
  TestClient replica(port);
  EXPECT_EQ(replica.call({"REPLICAOF", "127.0.0.1", std::to_string(pport)}), "+OK\r\n");
  primary.call({"SET", "after", "2"});
  primary.call({"INCR", "n"});
  std::string got;
  for (int i = 0; i < 100 && got != "$1\r\n1\r\n"; ++i) {
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    got = replica.call({"GET", "n"});
  }
  EXPECT_EQ(got, "$1\r\n1\r\n");
  EXPECT_EQ(replica.call({"GET", "before"}), "$1\r\n1\r\n");
  EXPECT_EQ(replica.call({"GET", "after"}), "$1\r\n2\r\n");
  ours.stop();
  t.join();
  ::kill(pid, SIGTERM);
  ::waitpid(pid, nullptr, 0);
}

}  // namespace
}  // namespace rd
