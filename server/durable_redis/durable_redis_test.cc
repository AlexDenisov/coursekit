#include "server/durable_redis/durable_redis.h"

#include <gtest/gtest.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#include <chrono>
#include <cstdio>
#include <memory>
#include <string>
#include <thread>

#include "common/test_client.h"

namespace rd {
namespace {

int64_t file_size(const std::string& path) {
  struct stat st;
  return ::stat(path.c_str(), &st) == 0 ? st.st_size : -1;
}

class Durable : public ::testing::Test {
 protected:
  Durable() {
    char dir[] = "/tmp/durableXXXXXX";
    dir_ = ::mkdtemp(dir);
  }
  ~Durable() override {
    stop();
    for (auto f : {"/appendonly.aof", "/dump.rdb", "/appendonly.aof.rewrite", "/dump.rdb.tmp"}) std::remove((dir_ + f).c_str());
    ::rmdir(dir_.c_str());
  }
  DurableRedisOptions options(bool aof = true, FsyncPolicy fsync = FsyncPolicy::kEverySec) {
    DurableRedisOptions o;
    o.dir = dir_;
    o.appendonly = aof;
    o.appendfsync = fsync;
    return o;
  }
  // A server in this process, on a thread.
  void start(DurableRedisOptions o) {
    server_ = std::make_unique<DurableRedis>(clock_, o);
    ASSERT_TRUE(server_->load()) << "load() failed";
    port_ = server_->listen("127.0.0.1", 0);
    ASSERT_NE(port_, 0);
    thread_ = std::thread([this] { server_->run(); });
  }
  void stop() {
    if (server_) server_->stop();
    if (thread_.joinable()) thread_.join();
    server_.reset();
  }
  SystemClock clock_;
  std::string dir_;
  std::unique_ptr<DurableRedis> server_;
  uint16_t port_ = 0;
  std::thread thread_;
};

TEST_F(Durable, FreshDirectoryStartsEmpty) {
  start(options());
  TestClient c(port_);
  EXPECT_EQ(c.call({"DBSIZE"}), ":0\r\n");
}

TEST_F(Durable, RdbSurvivesARestart) {
  start(options(/*aof=*/false));
  {
    TestClient c(port_);
    c.call({"SET", "k", "v"});
    c.call({"RPUSH", "l", "a", "b"});
    c.call({"SET", "t", "v", "EX", "1000"});
    EXPECT_EQ(c.call({"SAVE"}), "+OK\r\n");
    c.call({"SET", "unsaved", "x"});
  }
  stop();
  start(options(/*aof=*/false));
  TestClient c(port_);
  EXPECT_EQ(c.call({"GET", "k"}), "$1\r\nv\r\n");
  EXPECT_EQ(c.call({"LRANGE", "l", "0", "-1"}), "*2\r\n$1\r\na\r\n$1\r\nb\r\n");
  std::string ttl = c.call({"TTL", "t"});
  EXPECT_TRUE(ttl == ":1000\r\n" || ttl == ":999\r\n") << ttl;
  EXPECT_EQ(c.call({"EXISTS", "unsaved"}), ":0\r\n") << "only what SAVE captured comes back from an RDB";
}

TEST_F(Durable, AofSurvivesARestart) {
  start(options(true, FsyncPolicy::kAlways));
  {
    TestClient c(port_);
    for (int i = 0; i < 100; ++i) c.call({"INCR", "n"});
    c.call({"HSET", "h", "f", "v"});
  }
  stop();
  start(options(true, FsyncPolicy::kAlways));
  TestClient c(port_);
  EXPECT_EQ(c.call({"GET", "n"}), "$3\r\n100\r\n");
  EXPECT_EQ(c.call({"HGET", "h", "f"}), "$1\r\nv\r\n");
}

// The point: a server killed with SIGKILL (no chance to save anything) comes back with
// everything written more than a second before the kill.
TEST_F(Durable, KillDashNine) {
  uint16_t port;
  {
    Fd probe = listen_tcp("127.0.0.1", 0);
    port = local_port(probe.get());
  }
  pid_t pid = ::fork();
  ASSERT_GE(pid, 0);
  if (pid == 0) {  // the child: a real server process
    SystemClock clock;
    DurableRedis server(clock, options(true, FsyncPolicy::kEverySec));
    if (!server.load() || server.listen("127.0.0.1", port) == 0) ::_exit(1);
    server.run();
    ::_exit(0);
  }
  {
    TestClient c(port);
    for (int i = 0; i < 1000; ++i) c.send({"SET", "k" + std::to_string(i), std::to_string(i)});
    for (int i = 0; i < 1000; ++i) ASSERT_EQ(c.read_reply(), "+OK\r\n");
    EXPECT_EQ(c.call({"INCRBY", "counter", "42"}), ":42\r\n");
  }
  std::this_thread::sleep_for(std::chrono::milliseconds(1500));  // > 1 s: everysec has fsynced
  ::kill(pid, SIGKILL);
  ::waitpid(pid, nullptr, 0);

  start(options(true, FsyncPolicy::kEverySec));
  TestClient c(port_);
  EXPECT_EQ(c.call({"DBSIZE"}), ":1001\r\n");
  EXPECT_EQ(c.call({"GET", "k999"}), "$3\r\n999\r\n");
  EXPECT_EQ(c.call({"GET", "counter"}), "$2\r\n42\r\n");
}

TEST_F(Durable, AofIsPreferredOverRdb) {
  start(options(true, FsyncPolicy::kAlways));
  {
    TestClient c(port_);
    c.call({"SET", "k", "old"});
    ASSERT_EQ(c.call({"SAVE"}), "+OK\r\n");
    c.call({"SET", "k", "new"});
  }
  stop();
  start(options(true, FsyncPolicy::kAlways));
  TestClient c(port_);
  EXPECT_EQ(c.call({"GET", "k"}), "$3\r\nnew\r\n") << "the AOF is more recent than the snapshot";
}

TEST_F(Durable, BgsaveAndBgrewriteaof) {
  start(options(true, FsyncPolicy::kAlways));
  std::string aof = dir_ + "/appendonly.aof";
  {
    TestClient c(port_);
    for (int i = 0; i < 3000; ++i) c.send({"INCR", "n"});
    for (int i = 0; i < 3000; ++i) c.read_reply();
    int64_t before = file_size(aof);
    EXPECT_EQ(c.call({"BGSAVE"}), "+Background saving started\r\n");
    EXPECT_EQ(c.call({"BGREWRITEAOF"}), "+Background append only file rewriting started\r\n");
    bool shrunk = false;
    for (int i = 0; i < 100 && !shrunk; ++i) {
      std::this_thread::sleep_for(std::chrono::milliseconds(20));
      shrunk = file_size(aof) > 0 && file_size(aof) < before / 10;
    }
    EXPECT_TRUE(shrunk) << "the AOF wasn't rewritten";
    EXPECT_EQ(c.call({"INCR", "n"}), ":3001\r\n");
    EXPECT_GT(file_size(dir_ + "/dump.rdb"), 0);
  }
  stop();
  start(options(true, FsyncPolicy::kAlways));
  TestClient c(port_);
  EXPECT_EQ(c.call({"GET", "n"}), "$4\r\n3001\r\n");
}

TEST_F(Durable, CorruptRdbRefusesToStart) {
  {
    FILE* f = std::fopen((dir_ + "/dump.rdb").c_str(), "wb");
    std::fputs("REDIS0011garbage", f);
    std::fclose(f);
  }
  DurableRedis server(clock_, options(/*aof=*/false));
  EXPECT_FALSE(server.load());
}

}  // namespace
}  // namespace rd
