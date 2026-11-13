// The real test of an RDB writer: does Redis load the file?
#include <gtest/gtest.h>

#include <signal.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>

#include <memory>
#include <string>
#include <thread>

#include "common/test_client.h"
#include "db/hashes/hashes.h"
#include "db/lists/lists.h"
#include "db/sets/sets.h"
#include "db/zsets/zsets.h"
#include "persist/rdb_writer/rdb_writer.h"

extern char** environ;

namespace rd::rdb {
namespace {

std::string find_server() {
  for (const char* p : {"/opt/homebrew/bin/redis-server", "/usr/local/bin/redis-server", "/usr/bin/redis-server"}) {
    if (::access(p, X_OK) == 0) return p;
  }
  return "";
}

TEST(RealRedis, LoadsYourDump) {
  std::string server = find_server();
  if (server.empty()) GTEST_SKIP() << "redis-server not installed";
  SystemClock clock;
  Keyspace ks(clock);
  ks.set("str", std::make_unique<StringValue>("hello"));
  ks.set("num", std::make_unique<StringValue>("-12345"));
  auto l = std::make_unique<ListValue>();
  for (auto s : {"a", "b", "c"}) l->items.push_back(s);
  ks.set("list", std::move(l));
  auto h = std::make_unique<HashValue>();
  h->fields.insert("f", "v");
  ks.set("hash", std::move(h));
  auto s = std::make_unique<SetValue>();
  s->members.insert("m", true);
  ks.set("set", std::move(s));
  auto z = std::make_unique<ZSetValue>();
  z->set("a", 1.5);
  z->set("b", 2);
  ks.set("zset", std::move(z));
  ks.set("ttl", std::make_unique<StringValue>("v"));
  ks.set_expire("ttl", clock.now_ms() + 100'000);

  char dir[] = "/tmp/rdbrealXXXXXX";
  ASSERT_NE(::mkdtemp(dir), nullptr);
  ASSERT_TRUE(save_snapshot(ks, std::string(dir) + "/dump.rdb"));
  uint16_t port = static_cast<uint16_t>(20000 + ::getpid() % 20000);
  std::string port_s = std::to_string(port);
  const char* argv[] = {server.c_str(), "--port", port_s.c_str(), "--dir", dir, "--dbfilename", "dump.rdb",
                        "--save", "", "--appendonly", "no", nullptr};
  pid_t pid;
  ASSERT_EQ(::posix_spawn(&pid, server.c_str(), nullptr, nullptr, const_cast<char**>(argv), environ), 0);
  {
    TestClient c(port);
    EXPECT_EQ(c.call({"DBSIZE"}), ":7\r\n");
    EXPECT_EQ(c.call({"GET", "str"}), "$5\r\nhello\r\n");
    EXPECT_EQ(c.call({"GET", "num"}), "$6\r\n-12345\r\n");
    EXPECT_EQ(c.call({"LRANGE", "list", "0", "-1"}), "*3\r\n$1\r\na\r\n$1\r\nb\r\n$1\r\nc\r\n");
    EXPECT_EQ(c.call({"HGET", "hash", "f"}), "$1\r\nv\r\n");
    EXPECT_EQ(c.call({"SISMEMBER", "set", "m"}), ":1\r\n");
    EXPECT_EQ(c.call({"ZRANGE", "zset", "0", "-1", "WITHSCORES"}), "*4\r\n$1\r\na\r\n$3\r\n1.5\r\n$1\r\nb\r\n$1\r\n2\r\n");
    std::string ttl = c.call({"TTL", "ttl"});
    EXPECT_TRUE(ttl == ":100\r\n" || ttl == ":99\r\n") << ttl;
  }
  ::kill(pid, SIGTERM);
  ::waitpid(pid, nullptr, 0);
}

}  // namespace
}  // namespace rd::rdb
