#include "server/hello_cli/server.h"

#include <gtest/gtest.h>

#include <chrono>
#include <string>
#include <thread>

#include "common/test_client.h"
#include "db/strings/strings.h"

namespace rd {
namespace {

// A server with PING/ECHO and the string commands, serving `clients` clients one after
// the other on a background thread.
class HelloCli : public ::testing::Test {
 protected:
  void start(int clients = 1) {
    register_connection_commands(table_);
    register_string_commands(table_, db_);
    port_ = server_.listen("127.0.0.1", 0);
    ASSERT_NE(port_, 0);
    thread_ = std::thread([this, clients] {
      for (int i = 0; i < clients; ++i) server_.serve_one();
    });
  }
  void TearDown() override {
    if (thread_.joinable()) thread_.join();
  }

  StringDb db_;
  CommandTable table_;
  BlockingServer server_{table_};
  uint16_t port_ = 0;
  std::thread thread_;
};

TEST_F(HelloCli, Ping) {
  start();
  TestClient c(port_);
  EXPECT_EQ(c.call({"PING"}), "+PONG\r\n");
}

TEST_F(HelloCli, SetGetIncr) {
  start();
  TestClient c(port_);
  EXPECT_EQ(c.call({"SET", "k", "v"}), "+OK\r\n");
  EXPECT_EQ(c.call({"GET", "k"}), "$1\r\nv\r\n");
  EXPECT_EQ(c.call({"INCR", "n"}), ":1\r\n");
  EXPECT_EQ(c.call({"NOPE"}), "-ERR unknown command 'NOPE'\r\n");
}

// Pipelining: 200 requests in one write, 200 replies, in order.
TEST_F(HelloCli, PipelinedRequests) {
  start();
  TestClient c(port_);
  std::string batch, expected;
  for (int i = 0; i < 200; ++i) {
    batch += encode_request({"INCR", "n"});
    expected += ":" + std::to_string(i + 1) + "\r\n";
  }
  c.send_raw(batch);
  std::string got;
  for (int i = 0; i < 200; ++i) got += c.read_reply();
  EXPECT_EQ(got, expected);
}

// The pitfall: a request arriving in pieces. Nothing may be answered until it's whole.
TEST_F(HelloCli, RequestArrivingByteByByte) {
  start();
  TestClient c(port_);
  std::string req = encode_request({"ECHO", "piece by piece"});
  for (char ch : req) {
    EXPECT_TRUE(c.silent_for(0));
    c.send_raw(std::string(1, ch));
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
  }
  EXPECT_EQ(c.read_reply(), "$14\r\npiece by piece\r\n");
}

TEST_F(HelloCli, LargeValue) {
  start();
  TestClient c(port_);
  std::string big(3 << 20, 'z');
  EXPECT_EQ(c.call({"SET", "big", big}), "+OK\r\n");
  EXPECT_EQ(c.call({"GET", "big"}), "$" + std::to_string(big.size()) + "\r\n" + big + "\r\n");
}

// Redis (tools/cases/parser.txt, last group): replies to the requests before the bad
// one, then the error, then the connection is closed.
TEST_F(HelloCli, ProtocolErrorRepliesThenCloses) {
  start();
  TestClient c(port_);
  c.send_raw("*1\r\n$4\r\nPING\r\n*2\r\n$4\r\nECHO\r\n$2\r\nhi\r\n*1\r\n:5\r\n*1\r\n$4\r\nPING\r\n");
  EXPECT_EQ(c.read_until_close(),
            "+PONG\r\n$2\r\nhi\r\n-ERR Protocol error: expected '$', got ':'\r\n");
}

TEST_F(HelloCli, ClientsAreServedOneAfterAnother) {
  start(2);
  {
    TestClient a(port_);
    EXPECT_EQ(a.call({"SET", "shared", "1"}), "+OK\r\n");
  }
  TestClient b(port_);
  EXPECT_EQ(b.call({"GET", "shared"}), "$1\r\n1\r\n");
}

TEST_F(HelloCli, ClientHangingUpMidRequest) {
  start(2);
  {
    TestClient a(port_);
    a.send_raw("*2\r\n$4\r\nECHO\r\n$100\r\nonly a few bytes");
  }
  TestClient b(port_);
  EXPECT_EQ(b.call({"PING"}), "+PONG\r\n");
}

}  // namespace
}  // namespace rd
