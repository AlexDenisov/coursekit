#include "net/client/client.h"

#include <fcntl.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <gtest/gtest.h>

#include <string>

#include "common/test_client.h"  // encode_request
#include "db/strings/strings.h"

namespace rd {
namespace {

// A Client on one end of a socket pair; the test plays the remote client on the other.
class ClientTest : public ::testing::Test {
 protected:
  ClientTest() {
    register_connection_commands(table_);
    register_string_commands(table_, db_);
    int fds[2];
    EXPECT_EQ(::socketpair(AF_UNIX, SOCK_STREAM, 0, fds), 0);
    ::fcntl(fds[0], F_SETFL, ::fcntl(fds[0], F_GETFL) | O_NONBLOCK);
    int one = 1;
    ::setsockopt(fds[0], SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof one);
    server_fd_ = fds[0];
    peer_ = fds[1];
    ::fcntl(peer_, F_SETFL, ::fcntl(peer_, F_GETFL) | O_NONBLOCK);
  }
  ~ClientTest() override {
    if (peer_ >= 0) ::close(peer_);
  }

  Client make(ClientLimits limits = {}) { return Client(Fd(server_fd_), table_, limits); }

  // For small data (a socket pair's buffer holds a few KB).
  void send(std::string_view data) {
    ASSERT_EQ(::write(peer_, data.data(), data.size()), static_cast<ssize_t>(data.size()));
  }

  // Sends any amount of data while driving the client like an event loop would, and
  // returns everything the client sent back.
  std::string exchange(Client& c, std::string_view data) {
    std::string got;
    char buf[65536];
    for (int idle = 0; idle < 50;) {
      bool progress = false;
      if (!data.empty()) {
        ssize_t n = ::write(peer_, data.data(), data.size());
        if (n > 0) {
          data.remove_prefix(static_cast<size_t>(n));
          progress = true;
        }
      }
      if (c.on_readable() > 0) progress = true;
      c.on_writable();
      ssize_t n;
      while ((n = ::read(peer_, buf, sizeof buf)) > 0) {
        got.append(buf, static_cast<size_t>(n));
        progress = true;
      }
      idle = progress ? 0 : idle + 1;
    }
    return got;
  }

  // What the remote client has received so far.
  std::string received() {
    std::string out;
    char buf[65536];
    pollfd p{peer_, POLLIN, 0};
    while (::poll(&p, 1, 10) > 0) {
      ssize_t n = ::read(peer_, buf, sizeof buf);
      if (n <= 0) break;
      out.append(buf, static_cast<size_t>(n));
    }
    return out;
  }

  // Reads until nothing more arrives, then writes everything out (as an event loop
  // would, with both events firing).
  void pump(Client& c) {
    for (int i = 0; i < 10000 && c.on_readable() > 0; ++i) {
    }
    c.on_writable();
  }

  StringDb db_;
  CommandTable table_;
  int server_fd_ = -1;
  int peer_ = -1;
};

TEST_F(ClientTest, OneRequest) {
  Client c = make();
  send(encode_request({"PING"}));
  EXPECT_EQ(c.on_readable(), 14u);
  EXPECT_TRUE(c.wants_write());
  c.on_writable();
  EXPECT_FALSE(c.wants_write());
  EXPECT_EQ(received(), "+PONG\r\n");
  EXPECT_FALSE(c.should_close());
}

TEST_F(ClientTest, NothingToReadIsNotAnError) {
  Client c = make();
  EXPECT_EQ(c.on_readable(), 0u);
  EXPECT_FALSE(c.should_close());
}

TEST_F(ClientTest, RequestSplitAcrossReads) {
  Client c = make();
  std::string req = encode_request({"SET", "k", "v"});
  send(req.substr(0, 7));
  c.on_readable();
  EXPECT_FALSE(c.wants_write());
  send(req.substr(7));
  c.on_readable();
  c.on_writable();
  EXPECT_EQ(received(), "+OK\r\n");
}

TEST_F(ClientTest, PipelinedRequestsAllAnswered) {
  Client c = make();
  std::string batch, expected;
  for (int i = 1; i <= 3000; ++i) {
    batch += encode_request({"INCR", "n"});
    expected += ":" + std::to_string(i) + "\r\n";
  }
  EXPECT_EQ(exchange(c, batch), expected);  // ~80 KB each way
}

// Fairness: one readable event reads at most read_chunk bytes; the event loop calls
// again (level-triggered) for the rest, after serving others.
TEST_F(ClientTest, ReadsAtMostOneChunkPerEvent) {
  Client c = make({.max_query_buffer = size_t{1} << 30, .read_chunk = 1000});
  std::string batch;
  for (int i = 0; i < 300; ++i) batch += encode_request({"INCR", "n"});  // ~8 KB
  send(batch);
  EXPECT_EQ(c.on_readable(), 1000u);
  EXPECT_EQ(c.on_readable(), 1000u);
}

// Redis: replies to the requests before the bad one, then the error, then closes.
TEST_F(ClientTest, ProtocolErrorIsAnsweredThenCloses) {
  Client c = make();
  send("*1\r\n$4\r\nPING\r\n*2\r\n$4\r\nECHO\r\n$2\r\nhi\r\n*1\r\n:5\r\n*1\r\n$4\r\nPING\r\n");
  c.on_readable();
  EXPECT_FALSE(c.should_close()) << "the error reply hasn't been sent yet";
  c.on_writable();
  EXPECT_EQ(received(), "+PONG\r\n$2\r\nhi\r\n-ERR Protocol error: expected '$', got ':'\r\n");
  EXPECT_TRUE(c.should_close());
  send(encode_request({"PING"}));
  c.on_readable();
  c.on_writable();
  EXPECT_EQ(received(), "") << "nothing runs after a protocol error";
}

TEST_F(ClientTest, EofClosesAtOnce) {
  Client c = make();
  ::close(peer_);
  peer_ = -1;
  c.on_readable();
  EXPECT_TRUE(c.should_close());
}

// The pitfall: a client that sends a 1 GB "request" in pieces would make you buffer 1
// GB. Redis closes it once the unparsed input exceeds the limit.
TEST_F(ClientTest, QueryBufferLimitClosesWithoutReply) {
  Client c = make({.max_query_buffer = 1000, .read_chunk = 16 * 1024});
  send("*2\r\n$4\r\nECHO\r\n$5000\r\n" + std::string(1500, 'x'));
  c.on_readable();
  EXPECT_TRUE(c.should_close());
  EXPECT_FALSE(c.wants_write());
}

TEST_F(ClientTest, CompleteRequestsDontCountAgainstTheLimit) {
  Client c = make({.max_query_buffer = 100, .read_chunk = 16 * 1024});
  std::string batch, expected;
  for (int i = 1; i <= 50; ++i) {
    batch += encode_request({"INCR", "n"});  // 50 * 26 bytes, all complete
    expected += ":" + std::to_string(i) + "\r\n";
  }
  EXPECT_EQ(exchange(c, batch), expected);
  EXPECT_FALSE(c.should_close());
}

// The pitfall: the kernel takes only part of a big reply. The rest waits in the reply
// buffer for the next writable event, in order.
TEST_F(ClientTest, BigReplyIsWrittenInPieces) {
  Client c = make();
  std::string big(2 << 20, 'b');
  EXPECT_EQ(exchange(c, encode_request({"SET", "big", big})), "+OK\r\n");
  send(encode_request({"GET", "big"}));
  c.on_readable();
  c.on_writable();
  EXPECT_TRUE(c.wants_write()) << "2 MB fit in a socket buffer?";
  std::string got = exchange(c, "");  // keep writing whenever the socket has room
  EXPECT_FALSE(c.wants_write());
  EXPECT_TRUE(got == "$" + std::to_string(big.size()) + "\r\n" + big + "\r\n");
}

TEST_F(ClientTest, PushQueuesBytes) {
  Client c = make();
  c.push("*3\r\n$7\r\nmessage\r\n$2\r\nch\r\n$2\r\nhi\r\n");
  EXPECT_TRUE(c.wants_write());
  c.on_writable();
  EXPECT_EQ(received(), "*3\r\n$7\r\nmessage\r\n$2\r\nch\r\n$2\r\nhi\r\n");
}

TEST_F(ClientTest, CustomRunner) {
  Client c = make();
  std::vector<std::string> seen;
  c.set_runner([&](Client& self, std::span<const std::string> args, std::string& reply) {
    EXPECT_EQ(&self, &c);
    seen.push_back(args[0]);
    reply += "+QUEUED\r\n";
  });
  send(encode_request({"SET", "k", "v"}) + encode_request({"GET", "k"}));
  pump(c);
  EXPECT_EQ(received(), "+QUEUED\r\n+QUEUED\r\n");
  EXPECT_EQ(seen, (std::vector<std::string>{"SET", "GET"}));
}

TEST_F(ClientTest, BlockedClientRunsNothingUntilUnblocked) {
  Client c = make();
  c.block();
  send(encode_request({"INCR", "n"}) + encode_request({"INCR", "n"}));
  pump(c);
  EXPECT_EQ(received(), "");
  c.unblock();
  c.process_input();
  c.on_writable();
  EXPECT_EQ(received(), ":1\r\n:2\r\n");
}

TEST_F(ClientTest, CloseAfterReply) {
  Client c = make();
  c.push("+bye\r\n");
  c.close_after_reply();
  EXPECT_FALSE(c.should_close());
  c.on_writable();
  EXPECT_TRUE(c.should_close());
  EXPECT_EQ(received(), "+bye\r\n");
}

}  // namespace
}  // namespace rd
