#include "server/messaging_redis/messaging_redis.h"

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <vector>

#include "common/test_client.h"

namespace rd {
namespace {

class Messaging : public ::testing::Test {
 protected:
  void SetUp() override {
    server_ = std::make_unique<MessagingRedis>(clock_);
    port_ = server_->listen("127.0.0.1", 0);
    ASSERT_NE(port_, 0);
    thread_ = std::thread([this] { server_->run(); });
  }
  void TearDown() override {
    server_->stop();
    thread_.join();
  }
  SystemClock clock_;
  std::unique_ptr<MessagingRedis> server_;
  uint16_t port_ = 0;
  std::thread thread_;
};

// ★ A chat over pub/sub (replies as recorded from Redis by tools/messaging_reference.py).
TEST_F(Messaging, Chat) {
  TestClient alice(port_), bob(port_), carol(port_);
  EXPECT_EQ(alice.call({"SUBSCRIBE", "room"}), "*3\r\n$9\r\nsubscribe\r\n$4\r\nroom\r\n:1\r\n");
  EXPECT_EQ(carol.call({"PSUBSCRIBE", "ro*"}), "*3\r\n$10\r\npsubscribe\r\n$3\r\nro*\r\n:1\r\n");
  EXPECT_EQ(bob.call({"PUBLISH", "room", "hi all"}), ":2\r\n");
  EXPECT_EQ(alice.read_reply(), "*3\r\n$7\r\nmessage\r\n$4\r\nroom\r\n$6\r\nhi all\r\n");
  EXPECT_EQ(carol.read_reply(), "*4\r\n$8\r\npmessage\r\n$3\r\nro*\r\n$4\r\nroom\r\n$6\r\nhi all\r\n");
  EXPECT_EQ(alice.call({"GET", "k"}),
            "-ERR Can't execute 'get': only (P|S)SUBSCRIBE / (P|S)UNSUBSCRIBE / PING / QUIT / RESET are allowed in "
            "this context\r\n");
}

TEST_F(Messaging, DisconnectedSubscribersAreForgotten) {
  TestClient pub(port_);
  {
    TestClient sub(port_);
    sub.call({"SUBSCRIBE", "ch"});
  }
  std::string n;
  for (int i = 0; i < 100 && n != ":0\r\n"; ++i) {
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    n = pub.call({"PUBLISH", "ch", "x"});
  }
  EXPECT_EQ(n, ":0\r\n");
}

// ★ A job queue: workers block in BRPOP; every job is processed exactly once.
TEST_F(Messaging, JobQueue) {
  constexpr int kWorkers = 4, kJobs = 400;
  std::mutex mu;
  std::map<std::string, int> done;
  std::vector<std::thread> workers;
  for (int w = 0; w < kWorkers; ++w) {
    workers.emplace_back([&] {
      TestClient c(port_, 10000);
      for (;;) {
        std::string r = c.call({"BRPOP", "jobs", "0"});
        size_t at = r.rfind("$");
        std::string job = r.substr(r.find("\r\n", at) + 2);
        job.resize(job.size() - 2);
        if (job == "stop" || r.empty()) break;
        std::lock_guard<std::mutex> lock(mu);
        ++done[job];
      }
    });
  }
  std::this_thread::sleep_for(std::chrono::milliseconds(100));  // let them block
  TestClient producer(port_);
  for (int i = 0; i < kJobs; ++i) producer.send({"LPUSH", "jobs", "job" + std::to_string(i)});
  for (int i = 0; i < kJobs; ++i) producer.read_reply();
  for (int w = 0; w < kWorkers; ++w) producer.call({"LPUSH", "jobs", "stop"});
  for (auto& t : workers) t.join();
  EXPECT_EQ(done.size(), static_cast<size_t>(kJobs));
  for (auto& [job, n] : done) EXPECT_EQ(n, 1) << job << " processed " << n << " times";
}

// A blocked client blocks only itself, and what it sent after BLPOP waits its turn.
TEST_F(Messaging, BlockedClientKeepsItsOrder) {
  TestClient a(port_), b(port_);
  a.send_raw(encode_request({"BLPOP", "q", "0"}) + encode_request({"PING"}));
  EXPECT_EQ(b.call({"PING"}), "+PONG\r\n");
  EXPECT_TRUE(a.silent_for(100)) << "the PING after BLPOP must not run before BLPOP is served";
  EXPECT_EQ(b.call({"RPUSH", "q", "x"}), ":1\r\n");
  EXPECT_EQ(a.read_reply(), "*2\r\n$1\r\nq\r\n$1\r\nx\r\n");
  EXPECT_EQ(a.read_reply(), "+PONG\r\n");
}

TEST_F(Messaging, BlockingTimeout) {
  TestClient a(port_);
  auto t0 = std::chrono::steady_clock::now();
  EXPECT_EQ(a.call({"BLPOP", "q", "0.2"}), "*-1\r\n");
  EXPECT_GE(std::chrono::steady_clock::now() - t0, std::chrono::milliseconds(150));
  EXPECT_EQ(a.call({"PING"}), "+PONG\r\n");
}

TEST_F(Messaging, DisconnectedWaiterIsForgotten) {
  {
    TestClient a(port_);
    a.send({"BLPOP", "q", "0"});
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
  }
  std::this_thread::sleep_for(std::chrono::milliseconds(50));
  TestClient b(port_);
  EXPECT_EQ(b.call({"RPUSH", "q", "x"}), ":1\r\n");
  EXPECT_EQ(b.call({"LLEN", "q"}), ":1\r\n") << "nobody should pop for a closed connection";
}

// Recorded from Redis: BLPOP queued in MULTI doesn't block inside EXEC.
TEST_F(Messaging, BlpopInsideExecDoesNotBlock) {
  TestClient a(port_);
  EXPECT_EQ(a.call({"MULTI"}), "+OK\r\n");
  EXPECT_EQ(a.call({"BLPOP", "empty", "0"}), "+QUEUED\r\n");
  EXPECT_EQ(a.call({"EXEC"}), "*1\r\n*-1\r\n");
}

// ★ A counter incremented by GET + SET under WATCH from many clients: no increment is ever
// lost, because conflicting transactions fail and retry.
TEST_F(Messaging, WatchGuardedCounter) {
  constexpr int kClients = 8, kIncrements = 50;
  std::atomic<int> retries{0};
  std::vector<std::thread> threads;
  for (int t = 0; t < kClients; ++t) {
    threads.emplace_back([&] {
      TestClient c(port_);
      for (int i = 0; i < kIncrements; ++i) {
        for (;;) {
          c.call({"WATCH", "counter"});
          std::string v = c.call({"GET", "counter"});
          long n = v == "$-1\r\n" ? 0 : std::stol(v.substr(v.find("\r\n") + 2));
          c.call({"MULTI"});
          c.call({"SET", "counter", std::to_string(n + 1)});
          if (c.call({"EXEC"}) != "*-1\r\n") break;
          ++retries;
        }
      }
    });
  }
  for (auto& t : threads) t.join();
  TestClient c(port_);
  EXPECT_EQ(c.call({"GET", "counter"}), "$3\r\n400\r\n");
  std::printf("WATCH retries: %d\n", retries.load());
}

}  // namespace
}  // namespace rd
