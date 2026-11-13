#include "repl/replica/replica.h"

#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <gtest/gtest.h>

#include <chrono>
#include <fstream>
#include <functional>
#include <sstream>
#include <string>
#include <vector>

#include "common/test_client.h"  // encode_request
#include "db/lists/lists.h"

namespace rd {
namespace {

using Args = std::vector<std::string>;

std::string read_file(const char* path) {
  std::ifstream f(path, std::ios::binary);
  std::stringstream ss;
  ss << f.rdbuf();
  return ss.str();
}

// A scripted primary on a raw socket. Every wait "pumps" the replica's event loop, so the
// test, the fake primary and the replica share one thread.
class FakePrimary {
 public:
  explicit FakePrimary(std::function<void()> pump, uint16_t port = 0) : pump_(std::move(pump)) {
    listener_ = ::socket(AF_INET, SOCK_STREAM, 0);
    int one = 1;
    ::setsockopt(listener_, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
    sockaddr_in a{};
    a.sin_family = AF_INET;
    a.sin_port = htons(port);
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    ::bind(listener_, reinterpret_cast<sockaddr*>(&a), sizeof a);
    ::listen(listener_, 4);
    socklen_t len = sizeof a;
    ::getsockname(listener_, reinterpret_cast<sockaddr*>(&a), &len);
    port_ = ntohs(a.sin_port);
    ::fcntl(listener_, F_SETFL, O_NONBLOCK);
  }
  ~FakePrimary() {
    drop();
    ::close(listener_);
  }
  uint16_t port() const { return port_; }

  bool accept() {
    if (dead_) return false;
    bool ok = until([&] {
      conn_ = ::accept(listener_, nullptr, nullptr);
      if (conn_ >= 0) {
        int one = 1;
        ::setsockopt(conn_, SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof one);
        ::fcntl(conn_, F_SETFL, O_NONBLOCK);
      }
      return conn_ >= 0;
    });
    if (!ok) dead_ = true;  // fail fast from now on instead of timing out again and again
    return ok;
  }

  // The next request the replica sends, or {} after a timeout.
  Args next_request() {
    Args args;
    until([&] {
      fill();
      return parse(&args);
    });
    return args;
  }

  void send(std::string_view data) {
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    while (!data.empty() && conn_ >= 0 && std::chrono::steady_clock::now() < deadline) {
      ssize_t n = ::write(conn_, data.data(), data.size());
      if (n > 0) data.remove_prefix(static_cast<size_t>(n));
      pump_();
    }
  }
  void drop() {
    if (conn_ >= 0) ::close(conn_);
    conn_ = -1;
    in_.clear();
  }
  bool until(const std::function<bool()>& cond, int timeout_ms = 3000) {
    if (dead_) return cond();
    auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
    while (std::chrono::steady_clock::now() < deadline) {
      if (cond()) return true;
      pump_();
    }
    return false;
  }

 private:
  void fill() {
    char buf[4096];
    ssize_t n;
    while (conn_ >= 0 && (n = ::read(conn_, buf, sizeof buf)) > 0) in_.append(buf, static_cast<size_t>(n));
  }
  bool parse(Args* out) {
    if (in_.empty() || in_[0] != '*') return false;
    size_t e = in_.find("\r\n");
    if (e == std::string::npos) return false;
    long n = std::stol(in_.substr(1, e - 1));
    size_t p = e + 2;
    Args args;
    for (long i = 0; i < n; ++i) {
      size_t le = in_.find("\r\n", p);
      if (le == std::string::npos) return false;
      size_t len = std::stoul(in_.substr(p + 1, le - p - 1));
      if (in_.size() < le + 2 + len + 2) return false;
      args.push_back(in_.substr(le + 2, len));
      p = le + 2 + len + 2;
    }
    in_.erase(0, p);
    *out = args;
    return true;
  }
  std::function<void()> pump_;
  int listener_ = -1, conn_ = -1;
  bool dead_ = false;
  uint16_t port_ = 0;
  std::string in_;
};

class ReplicaTest : public ::testing::Test {
 protected:
  ReplicaTest() {
    register_keyspace_commands(table_, ks_);
    register_list_commands(table_, ks_);
  }
  void pump() { loop_.process_events(5); }

  // Accepts the replica and checks its handshake up to (and returning) the PSYNC.
  Args handshake() {
    EXPECT_TRUE(primary_.accept()) << "the replica never connected";
    EXPECT_EQ(primary_.next_request(), (Args{"PING"}));
    primary_.send("+PONG\r\n");
    EXPECT_EQ(primary_.next_request(), (Args{"REPLCONF", "listening-port", "6380"}));
    primary_.send("+OK\r\n");
    EXPECT_EQ(primary_.next_request(), (Args{"REPLCONF", "capa", "psync2"}));
    primary_.send("+OK\r\n");
    return primary_.next_request();
  }

  // The RDB that real Redis 8.10 wrote (persist/rdb_reader's fixture: 17 keys).
  void full_sync(const std::string& replid, uint64_t offset) {
    std::string rdb = read_file("persist/rdb_reader/testdata/redis8_plain.rdb");
    ASSERT_FALSE(rdb.empty());
    primary_.send("+FULLRESYNC " + replid + " " + std::to_string(offset) + "\r\n");
    primary_.send("\n\n");  // keepalives while the primary "prepares" the RDB
    primary_.send("$" + std::to_string(rdb.size()) + "\r\n" + rdb);
  }

  SystemClock clock_;
  Keyspace ks_{clock_};
  CommandTable table_;
  EventLoop loop_{clock_};
  Replica replica_{ks_, table_, loop_, 6380};
  FakePrimary primary_{[this] { pump(); }};
  const std::string replid_ = std::string(40, 'a');
};

TEST_F(ReplicaTest, HandshakeAsksForAFullSyncTheFirstTime) {
  replica_.start("127.0.0.1", primary_.port());
  EXPECT_EQ(handshake(), (Args{"PSYNC", "?", "-1"}));
  EXPECT_EQ(replica_.state(), Replica::State::kHandshake);
}

TEST_F(ReplicaTest, FullSyncLoadsTheRdbThenAppliesTheStream) {
  ks_.set("stale", std::make_unique<StringValue>("from before"));
  replica_.start("127.0.0.1", primary_.port());
  handshake();
  full_sync(replid_, 1000);
  std::string stream = encode_request({"SELECT", "0"}) + encode_request({"SET", "a", "1"}) +
                       encode_request({"RPUSH", "q", "x", "y"}) + encode_request({"INCR", "int"});
  primary_.send(stream);
  ASSERT_TRUE(primary_.until([&] { return replica_.offset() == 1000 + stream.size(); }))
      << "offset " << replica_.offset() << ", expected " << 1000 + stream.size();
  EXPECT_EQ(replica_.state(), Replica::State::kStreaming);
  EXPECT_EQ(replica_.replid(), replid_);
  EXPECT_EQ(replica_.full_syncs(), 1u);
  EXPECT_EQ(ks_.find("stale"), nullptr) << "a full sync replaces everything";
  EXPECT_EQ(ks_.size(), 17u + 2);
  EXPECT_EQ(static_cast<StringValue*>(ks_.find("a"))->s, "1");
  EXPECT_EQ(static_cast<StringValue*>(ks_.find("int"))->s, "12346") << "fixture value + the INCR";
  EXPECT_EQ(static_cast<StringValue*>(ks_.find("str"))->s, "hello") << "from the RDB";
}

TEST_F(ReplicaTest, AcksItsOffsetEverySecond) {
  replica_.start("127.0.0.1", primary_.port());
  handshake();
  full_sync(replid_, 0);
  std::string cmd = encode_request({"SET", "a", "1"});
  primary_.send(cmd);
  Args ack = primary_.next_request();
  EXPECT_EQ(ack, (Args{"REPLCONF", "ACK", std::to_string(cmd.size())}));
}

TEST_F(ReplicaTest, AnswersGetack) {
  replica_.start("127.0.0.1", primary_.port());
  handshake();
  full_sync(replid_, 50);
  std::string cmd = encode_request({"SET", "a", "1"});
  primary_.send(cmd + encode_request({"REPLCONF", "GETACK", "*"}));
  EXPECT_EQ(primary_.next_request(), (Args{"REPLCONF", "ACK", std::to_string(50 + cmd.size())}))
      << "the offset before the GETACK itself";
}

// The point of the backlog: after a short disconnect, only the missing bytes are sent.
TEST_F(ReplicaTest, ReconnectsWithPartialResync) {
  replica_.start("127.0.0.1", primary_.port());
  handshake();
  full_sync(replid_, 0);
  std::string cmd = encode_request({"SET", "a", "1"});
  primary_.send(cmd);
  ASSERT_TRUE(primary_.until([&] { return replica_.offset() == cmd.size(); }));
  primary_.drop();  // the network hiccups

  EXPECT_EQ(handshake(), (Args{"PSYNC", replid_, std::to_string(cmd.size() + 1)}));
  std::string new_id(40, 'b');
  primary_.send("+CONTINUE " + new_id + "\r\n");
  std::string more = encode_request({"SET", "b", "2"});
  primary_.send(more);
  ASSERT_TRUE(primary_.until([&] { return replica_.offset() == cmd.size() + more.size(); }));
  EXPECT_EQ(replica_.partial_syncs(), 1u);
  EXPECT_EQ(replica_.full_syncs(), 1u);
  EXPECT_EQ(replica_.replid(), new_id) << "psync2: CONTINUE may announce a new replication id";
  EXPECT_NE(ks_.find("a"), nullptr) << "a partial resync keeps the data";
  EXPECT_NE(ks_.find("b"), nullptr);
}

// The primary isn't up yet: connections are refused. The replica keeps retrying and
// gets through once it starts.
TEST_F(ReplicaTest, RetriesUntilThePrimaryIsUp) {
  uint16_t port;
  {
    FakePrimary probe([] {});
    port = probe.port();
  }  // closed: nothing listens on `port` now
  replica_.start("127.0.0.1", port);
  for (int i = 0; i < 60; ++i) pump();  // ~300 ms of refused attempts
  EXPECT_EQ(replica_.state(), Replica::State::kConnecting);
  FakePrimary late([this] { pump(); }, port);
  ASSERT_TRUE(late.accept()) << "the replica stopped retrying";
  EXPECT_EQ(late.next_request(), (Args{"PING"}));
}

TEST_F(ReplicaTest, StopDisconnectsAndKeepsTheData) {
  replica_.start("127.0.0.1", primary_.port());
  handshake();
  full_sync(replid_, 0);
  primary_.send(encode_request({"SET", "a", "1"}));
  ASSERT_TRUE(primary_.until([&] { return ks_.find("a") != nullptr; }));
  replica_.stop();
  EXPECT_EQ(replica_.state(), Replica::State::kNone);
  for (int i = 0; i < 50; ++i) pump();
  EXPECT_NE(ks_.find("a"), nullptr);
}

TEST_F(ReplicaTest, ReplicaofCommand) {
  register_replicaof_command(table_, replica_);
  std::string reply;
  table_.dispatch(Args{"REPLICAOF", "127.0.0.1", std::to_string(primary_.port())}, reply);
  EXPECT_EQ(reply, "+OK\r\n");
  EXPECT_EQ(handshake()[0], "PSYNC");
  reply.clear();
  table_.dispatch(Args{"REPLICAOF", "NO", "ONE"}, reply);
  EXPECT_EQ(reply, "+OK\r\n");
  EXPECT_EQ(replica_.state(), Replica::State::kNone);
}

}  // namespace
}  // namespace rd
