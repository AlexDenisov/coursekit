// Your replica following the real thing.
#include <gtest/gtest.h>

#include <signal.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>

#include <chrono>
#include <string>

#include "common/test_client.h"
#include "db/lists/lists.h"
#include "repl/replica/replica.h"

extern char** environ;

namespace rd {
namespace {

std::string find_server() {
  for (const char* p : {"/opt/homebrew/bin/redis-server", "/usr/local/bin/redis-server", "/usr/bin/redis-server"}) {
    if (::access(p, X_OK) == 0) return p;
  }
  return "";
}

TEST(RealPrimary, ReplicaFollowsRedis) {
  std::string server = find_server();
  if (server.empty()) GTEST_SKIP() << "redis-server not installed";
  uint16_t port = static_cast<uint16_t>(21000 + ::getpid() % 20000);
  std::string port_s = std::to_string(port);
  char dir[] = "/tmp/replprimXXXXXX";
  ::mkdtemp(dir);
  // rdbcompression no: the full sync then doesn't need the optional LZF part of
  // persist/rdb_reader. (With it implemented, drop this flag.)
  const char* argv[] = {server.c_str(), "--port", port_s.c_str(), "--dir", dir, "--save", "", "--appendonly",
                        "no", "--rdbcompression", "no", nullptr};
  pid_t pid;
  ASSERT_EQ(::posix_spawn(&pid, server.c_str(), nullptr, nullptr, const_cast<char**>(argv), environ), 0);
  TestClient primary(port);
  ASSERT_EQ(primary.call({"SET", "before", "1"}), "+OK\r\n");
  ASSERT_EQ(primary.call({"RPUSH", "list", "a", "b"}), ":2\r\n");

  SystemClock clock;
  Keyspace ks(clock);
  CommandTable table;
  register_keyspace_commands(table, ks);
  register_list_commands(table, ks);
  EventLoop loop(clock);
  Replica replica(ks, table, loop, 6390);
  replica.start("127.0.0.1", port);
  auto pump_until = [&](auto cond) {
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (!cond() && std::chrono::steady_clock::now() < deadline) loop.process_events(10);
    return cond();
  };
  EXPECT_TRUE(pump_until([&] { return replica.state() == Replica::State::kStreaming; }));
  EXPECT_NE(ks.find("before"), nullptr) << "not in the full sync";

  primary.call({"SET", "after", "2"});
  primary.call({"INCR", "n"});
  primary.call({"INCR", "n"});
  primary.call({"LPUSH", "list", "z"});
  EXPECT_TRUE(pump_until([&] { return ks.find("n") && static_cast<StringValue*>(ks.find("n"))->s == "2"; }));
  EXPECT_NE(ks.find("after"), nullptr);
  std::string role = primary.call({"INFO", "replication"});
  EXPECT_NE(role.find("connected_slaves:1"), std::string::npos) << role;
  EXPECT_TRUE(pump_until([&] {
    std::string info = primary.call({"INFO", "replication"});
    return info.find("offset=" + std::to_string(replica.offset())) != std::string::npos;
  })) << "the primary never saw our REPLCONF ACK";

  replica.stop();
  ::kill(pid, SIGTERM);
  ::waitpid(pid, nullptr, 0);
}

}  // namespace
}  // namespace rd
