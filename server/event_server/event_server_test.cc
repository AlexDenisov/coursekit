#include "server/event_server/event_server.h"

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "common/test_client.h"
#include "db/strings/strings.h"

namespace rd {
namespace {

class EventServerTest : public ::testing::Test {
 protected:
  void start(ClientLimits limits = {}) {
    register_connection_commands(table_);
    register_string_commands(table_, db_);
    server_ = std::make_unique<EventServer>(table_, clock_, limits);
    port_ = server_->listen("127.0.0.1", 0);
    ASSERT_NE(port_, 0);
    thread_ = std::thread([this] { server_->run(); });
  }
  void TearDown() override {
    if (server_) server_->stop();
    if (thread_.joinable()) thread_.join();
  }
  // client_count() is read from the test thread while the loop runs: only for polling
  // until a number settles, as below.
  bool eventually_clients(size_t n) {
    for (int i = 0; i < 200; ++i) {
      if (server_->client_count() == n) return true;
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    return false;
  }

  SystemClock clock_;
  StringDb db_;
  CommandTable table_;
  std::unique_ptr<EventServer> server_;
  uint16_t port_ = 0;
  std::thread thread_;
};

TEST_F(EventServerTest, Ping) {
  start();
  TestClient c(port_);
  EXPECT_EQ(c.call({"PING"}), "+PONG\r\n");
  EXPECT_EQ(c.call({"SET", "k", "v"}), "+OK\r\n");
  EXPECT_EQ(c.call({"GET", "k"}), "$1\r\nv\r\n");
}

// The point of the whole track: in server/hello_cli, a client that sent half a request
// blocked everyone. Here it only waits for itself.
TEST_F(EventServerTest, SlowClientDoesNotBlockOthers) {
  start();
  TestClient slow(port_);
  slow.send_raw("*2\r\n$4\r\nECHO\r\n$5\r\nhel");
  TestClient fast(port_);
  EXPECT_EQ(fast.call({"PING"}), "+PONG\r\n");
  slow.send_raw("lo\r\n");
  EXPECT_EQ(slow.read_reply(), "$5\r\nhello\r\n");
}

TEST_F(EventServerTest, ManyClientsAtOnce) {
  start();
  constexpr int kThreads = 20, kIncrs = 200;
  std::vector<std::thread> threads;
  std::atomic<int> errors{0};
  for (int t = 0; t < kThreads; ++t) {
    threads.emplace_back([&] {
      TestClient c(port_);
      for (int i = 0; i < kIncrs; ++i) {
        std::string r = c.call({"INCR", "counter"});
        if (r.empty() || r[0] != ':') ++errors;
      }
    });
  }
  for (auto& t : threads) t.join();
  EXPECT_EQ(errors, 0);
  TestClient c(port_);
  EXPECT_EQ(c.call({"GET", "counter"}), "$4\r\n4000\r\n") << "lost or duplicated INCRs";
}

TEST_F(EventServerTest, BigPipelinedBatch) {
  start();
  TestClient c(port_, 10000);
  std::string batch;
  for (int i = 0; i < 20000; ++i) batch += encode_request({"INCR", "n"});
  std::thread writer([&] { c.send_raw(batch); });  // ~0.5 MB: write and read concurrently
  std::string last;
  for (int i = 0; i < 20000; ++i) last = c.read_reply();
  writer.join();
  EXPECT_EQ(last, ":20000\r\n");
}

// A client that never reads its replies fills its socket buffer; the server must keep
// the rest in the reply buffer and keep serving everyone else.
TEST_F(EventServerTest, ClientThatDoesNotReadDoesNotBlockOthers) {
  start();
  TestClient setter(port_);
  std::string big(1 << 20, 'x');
  ASSERT_EQ(setter.call({"SET", "big", big}), "+OK\r\n");
  TestClient hog(port_);
  for (int i = 0; i < 8; ++i) hog.send({"GET", "big"});  // 8 MB of replies, never read
  std::this_thread::sleep_for(std::chrono::milliseconds(100));
  TestClient other(port_);
  EXPECT_EQ(other.call({"PING"}), "+PONG\r\n");
}

TEST_F(EventServerTest, DisconnectedClientsAreFreed) {
  start();
  {
    std::vector<std::unique_ptr<TestClient>> cs;
    for (int i = 0; i < 10; ++i) {
      cs.push_back(std::make_unique<TestClient>(port_));
      EXPECT_EQ(cs.back()->call({"PING"}), "+PONG\r\n");
    }
    EXPECT_TRUE(eventually_clients(10));
  }
  EXPECT_TRUE(eventually_clients(0)) << "closed connections are still in client_count()";
}

TEST_F(EventServerTest, CloseHookRunsOncePerClient) {
  register_connection_commands(table_);
  server_ = std::make_unique<EventServer>(table_, clock_);
  std::atomic<int> opened{0}, closed{0};
  server_->set_new_client_hook([&](Client&) { ++opened; });
  server_->set_close_hook([&](Client&) { ++closed; });
  port_ = server_->listen("127.0.0.1", 0);
  thread_ = std::thread([this] { server_->run(); });
  for (int i = 0; i < 5; ++i) {
    TestClient c(port_);
    EXPECT_EQ(c.call({"PING"}), "+PONG\r\n");
  }
  EXPECT_TRUE(eventually_clients(0));
  EXPECT_EQ(opened, 5);
  EXPECT_EQ(closed, 5);
}

TEST_F(EventServerTest, ProtocolErrorClosesOnlyThatClient) {
  start();
  TestClient bad(port_), good(port_);
  bad.send_raw("*1\r\n:5\r\n");
  EXPECT_EQ(bad.read_until_close(), "-ERR Protocol error: expected '$', got ':'\r\n");
  EXPECT_EQ(good.call({"PING"}), "+PONG\r\n");
}

TEST_F(EventServerTest, QueryBufferLimit) {
  start({.max_query_buffer = 4096, .read_chunk = 16 * 1024});
  TestClient c(port_);
  c.send_raw("*2\r\n$4\r\nECHO\r\n$100000\r\n" + std::string(8000, 'x'));
  EXPECT_TRUE(c.closed_by_peer());
}

// Session::push from one client's command must reach another client even though that
// other client has nothing to read: the before-sleep flush.
TEST_F(EventServerTest, PushedRepliesAreFlushed) {
  register_connection_commands(table_);
  register_string_commands(table_, db_);
  Session* target = nullptr;
  table_.add({"hello", 1, [&](CommandContext& c) {
                if (target == nullptr) {
                  target = c.session;  // the first caller becomes the target
                } else {
                  target->push("+pushed\r\n");
                }
                c.reply += "+OK\r\n";
              }});
  server_ = std::make_unique<EventServer>(table_, clock_);
  port_ = server_->listen("127.0.0.1", 0);
  thread_ = std::thread([this] { server_->run(); });
  TestClient a(port_), b(port_);
  EXPECT_EQ(a.call({"HELLO"}), "+OK\r\n");
  EXPECT_EQ(b.call({"HELLO"}), "+OK\r\n");
  EXPECT_EQ(a.read_reply(), "+pushed\r\n");
}

}  // namespace
}  // namespace rd
