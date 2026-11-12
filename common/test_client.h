#pragma once

// A tiny blocking RESP2 client for tests of the servers, written independently of the
// exercises (it must never rely on the code under test). Fails the current test,
// instead of hanging, when the server doesn't answer in time.
//
//   rd::TestClient c(port);
//   EXPECT_EQ(c.call({"SET", "k", "v"}), "+OK\r\n");
//   c.send_raw("*1\r\n$4\r\nPI");      // half a request
//   EXPECT_TRUE(c.closed_by_peer());

#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <gtest/gtest.h>

#include <chrono>
#include <cstdint>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace rd {

inline std::string encode_request(const std::vector<std::string>& args) {
  std::string out = "*" + std::to_string(args.size()) + "\r\n";
  for (const auto& a : args) out += "$" + std::to_string(a.size()) + "\r\n" + a + "\r\n";
  return out;
}

class TestClient {
 public:
  // Connects to 127.0.0.1:port, retrying for up to 2 s while the server starts.
  explicit TestClient(uint16_t port, int timeout_ms = 3000) : timeout_ms_(timeout_ms) {
    if (port == 0) {
      ADD_FAILURE() << "TestClient: port 0 (the server didn't start listening)";
      return;
    }
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    for (;;) {
      fd_ = ::socket(AF_INET, SOCK_STREAM, 0);
      int one = 1;
      ::setsockopt(fd_, SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof one);
      ::setsockopt(fd_, IPPROTO_TCP, TCP_NODELAY, &one, sizeof one);
      sockaddr_in addr{};
      addr.sin_family = AF_INET;
      addr.sin_port = htons(port);
      addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
      if (::connect(fd_, reinterpret_cast<sockaddr*>(&addr), sizeof addr) == 0) return;
      ::close(fd_);
      fd_ = -1;
      if (std::chrono::steady_clock::now() > deadline) {
        ADD_FAILURE() << "TestClient: can't connect to port " << port;
        return;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
  }
  ~TestClient() { close(); }
  TestClient(const TestClient&) = delete;
  TestClient& operator=(const TestClient&) = delete;

  bool connected() const { return fd_ >= 0; }
  int fd() const { return fd_; }
  void close() {
    if (fd_ >= 0) ::close(fd_);
    fd_ = -1;
  }

  void send_raw(std::string_view data) {
    while (fd_ >= 0 && !data.empty()) {
      ssize_t n = ::write(fd_, data.data(), data.size());
      if (n <= 0) {
        ADD_FAILURE() << "TestClient: write failed";
        return;
      }
      data.remove_prefix(static_cast<size_t>(n));
    }
  }
  void send(const std::vector<std::string>& args) { send_raw(encode_request(args)); }

  // One complete RESP2 reply, as raw bytes. Empty (and a test failure) on timeout or
  // EOF.
  std::string read_reply() {
    size_t end = 0;
    while (!reply_end(0, &end)) {
      if (!fill()) {
        ADD_FAILURE() << "TestClient: no complete reply; got so far: "
                      << ::testing::PrintToString(buf_);
        close();  // later reads fail at once instead of each waiting for the timeout
        buf_.clear();
        return "";
      }
    }
    std::string r = buf_.substr(0, end);
    buf_.erase(0, end);
    return r;
  }

  std::string call(const std::vector<std::string>& args) {
    send(args);
    return read_reply();
  }

  // Everything the server sends until it closes the connection (or the timeout).
  std::string read_until_close() {
    while (fill()) {
    }
    std::string r = std::move(buf_);
    buf_.clear();
    return r;
  }

  // True if the server closes the connection within the timeout (unread data is
  // dropped).
  bool closed_by_peer(int timeout_ms = 2000) {
    auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
    char tmp[4096];
    while (std::chrono::steady_clock::now() < deadline) {
      pollfd p{fd_, POLLIN, 0};
      if (::poll(&p, 1, 50) > 0) {
        ssize_t n = ::read(fd_, tmp, sizeof tmp);
        if (n <= 0) return true;
      }
    }
    return false;
  }

  // True if nothing arrives within `ms`.
  bool silent_for(int ms) {
    pollfd p{fd_, POLLIN, 0};
    return buf_.empty() && ::poll(&p, 1, ms) == 0;
  }

 private:
  bool fill() {
    pollfd p{fd_, POLLIN, 0};
    if (fd_ < 0 || ::poll(&p, 1, timeout_ms_) <= 0) return false;
    char tmp[65536];
    ssize_t n = ::read(fd_, tmp, sizeof tmp);
    if (n <= 0) return false;
    buf_.append(tmp, static_cast<size_t>(n));
    return true;
  }

  // Is there a complete reply starting at buf_[pos]? Sets *end past it.
  bool reply_end(size_t pos, size_t* end) const {
    size_t cr = buf_.find("\r\n", pos);
    if (cr == std::string::npos) return false;
    char t = buf_[pos];
    if (t == '+' || t == '-' || t == ':') {
      *end = cr + 2;
      return true;
    }
    long long n = std::stoll(buf_.substr(pos + 1, cr - pos - 1));
    if (t == '$') {
      if (n < 0) {
        *end = cr + 2;
        return true;
      }
      if (buf_.size() < cr + 2 + static_cast<size_t>(n) + 2) return false;
      *end = cr + 2 + static_cast<size_t>(n) + 2;
      return true;
    }
    if (t == '*') {
      size_t p = cr + 2;
      for (long long i = 0; i < n; ++i) {
        if (!reply_end(p, &p)) return false;
      }
      *end = p;
      return true;
    }
    ADD_FAILURE() << "TestClient: not RESP2: " << ::testing::PrintToString(buf_.substr(pos));
    return false;
  }

  int fd_ = -1;
  int timeout_ms_;
  std::string buf_;
};

}  // namespace rd
