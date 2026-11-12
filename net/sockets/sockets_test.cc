#include "net/sockets/sockets.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>

#include <gtest/gtest.h>

#include <chrono>
#include <string>
#include <thread>

// The tests use raw POSIX calls for the "other side" of each connection, so they never
// depend on the code under test.

namespace rd {
namespace {

int raw_connect(uint16_t port) {
  int fd = ::socket(AF_INET, SOCK_STREAM, 0);
  sockaddr_in a{};
  a.sin_family = AF_INET;
  a.sin_port = htons(port);
  a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  if (::connect(fd, reinterpret_cast<sockaddr*>(&a), sizeof a) != 0) {
    ::close(fd);
    return -1;
  }
  return fd;
}

int raw_listen(uint16_t* port) {
  int fd = ::socket(AF_INET, SOCK_STREAM, 0);
  sockaddr_in a{};
  a.sin_family = AF_INET;
  a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  ::bind(fd, reinterpret_cast<sockaddr*>(&a), sizeof a);
  ::listen(fd, 16);
  socklen_t len = sizeof a;
  ::getsockname(fd, reinterpret_cast<sockaddr*>(&a), &len);
  *port = ntohs(a.sin_port);
  return fd;
}

std::string raw_read_all(int fd) {
  std::string out;
  char buf[65536];
  for (;;) {
    pollfd p{fd, POLLIN, 0};
    if (::poll(&p, 1, 5000) <= 0) break;
    ssize_t n = ::read(fd, buf, sizeof buf);
    if (n <= 0) break;
    out.append(buf, static_cast<size_t>(n));
  }
  return out;
}

TEST(Sockets, ListenOnAnyFreePort) {
  Fd l = listen_tcp("127.0.0.1", 0);
  ASSERT_TRUE(l.valid());
  uint16_t port = local_port(l.get());
  EXPECT_NE(port, 0);
  int c = raw_connect(port);
  EXPECT_GE(c, 0) << "nothing accepts connections on the returned port";
  ::close(c);
}

TEST(Sockets, BadHostIsAnError) {
  EXPECT_FALSE(listen_tcp("not an address", 0).valid());
  EXPECT_FALSE(listen_tcp("999.0.0.1", 0).valid());
}

TEST(Sockets, PortInUseIsAnError) {
  Fd a = listen_tcp("127.0.0.1", 0);
  ASSERT_TRUE(a.valid());
  EXPECT_FALSE(listen_tcp("127.0.0.1", local_port(a.get())).valid());
}

// The pitfall: without SO_REUSEADDR, a restarted server can't bind its port for a
// minute or two after the previous one closed with connections in TIME_WAIT.
TEST(Sockets, PortIsReusableRightAfterClose) {
  uint16_t port;
  {
    Fd l = listen_tcp("127.0.0.1", 0);
    ASSERT_TRUE(l.valid());
    port = local_port(l.get());
    int c = raw_connect(port);
    Fd s = accept_connection(l.get());
    ASSERT_TRUE(s.valid());
    s.reset();  // the server closes first: its side goes to TIME_WAIT
    ::close(c);
  }
  Fd again = listen_tcp("127.0.0.1", port);
  EXPECT_TRUE(again.valid()) << "rebinding port " << port << " failed: errno " << errno;
}

TEST(Sockets, AcceptAndReadSome) {
  Fd l = listen_tcp("127.0.0.1", 0);
  int c = raw_connect(local_port(l.get()));
  Fd s = accept_connection(l.get());
  ASSERT_TRUE(s.valid());
  ASSERT_EQ(::write(c, "hello", 5), 5);
  char buf[16];
  ssize_t n = read_some(s.get(), buf, sizeof buf);
  ASSERT_GT(n, 0);
  EXPECT_EQ(std::string(buf, static_cast<size_t>(n)), std::string("hello").substr(0, n));
  ::close(c);
  // Drain whatever is left, then expect end of stream.
  while ((n = read_some(s.get(), buf, sizeof buf)) > 0) {
  }
  EXPECT_EQ(n, 0);
}

TEST(Sockets, ConnectTcp) {
  uint16_t port;
  int l = raw_listen(&port);
  Fd c = connect_tcp("127.0.0.1", port);
  ASSERT_TRUE(c.valid());
  int s = ::accept(l, nullptr, nullptr);
  ASSERT_GE(s, 0);
  ASSERT_TRUE(write_all(c.get(), "ping"));
  char buf[4];
  ASSERT_EQ(::read(s, buf, 4), 4);
  ::close(s);
  ::close(l);
}

TEST(Sockets, ConnectToClosedPortFails) {
  uint16_t port;
  int l = raw_listen(&port);
  ::close(l);
  EXPECT_FALSE(connect_tcp("127.0.0.1", port).valid());
}

// The pitfall: write() may write less than asked once the kernel's buffers fill up.
// 8 MB is far more than a socket buffer holds while the reader isn't reading.
TEST(Sockets, WriteAllHandlesShortWrites) {
  Fd l = listen_tcp("127.0.0.1", 0);
  int c = raw_connect(local_port(l.get()));
  Fd s = accept_connection(l.get());
  std::string data(8 << 20, '\0');
  for (size_t i = 0; i < data.size(); ++i) data[i] = static_cast<char>(i * 7 + i / 4096);
  std::string got;
  std::thread reader([&] {
    std::this_thread::sleep_for(std::chrono::milliseconds(100));  // let the buffers fill
    got = raw_read_all(c);
  });
  EXPECT_TRUE(write_all(s.get(), data));
  s.reset();
  reader.join();
  ::close(c);
  EXPECT_EQ(got.size(), data.size());
  EXPECT_TRUE(got == data) << "bytes were lost or reordered";
}

// The pitfall: writing to a connection the peer has closed raises SIGPIPE, whose default
// action kills the process. A server must survive any client hanging up.
TEST(Sockets, WriteToClosedPeerFailsWithoutKillingTheProcess) {
  Fd l = listen_tcp("127.0.0.1", 0);
  int c = raw_connect(local_port(l.get()));
  Fd s = accept_connection(l.get());
  ::close(c);
  std::this_thread::sleep_for(std::chrono::milliseconds(50));
  std::string chunk(1 << 20, 'x');
  bool ok = true;
  for (int i = 0; i < 20 && ok; ++i) ok = write_all(s.get(), chunk);
  EXPECT_FALSE(ok) << "writes to a closed connection must eventually fail";
}

TEST(Sockets, EchoOneClient) {
  Fd l = listen_tcp("127.0.0.1", 0);
  uint16_t port = local_port(l.get());
  size_t echoed = 0;
  std::thread server([&] { echoed = echo_one_client(l.get()); });
  int c = raw_connect(port);
  std::string msg;
  for (int i = 0; i < 1000; ++i) msg += "line " + std::to_string(i) + "\n";
  std::string got;
  std::thread reader([&] { got = raw_read_all(c); });
  for (size_t i = 0; i < msg.size(); i += 777) {
    std::string_view part = std::string_view(msg).substr(i, 777);
    ASSERT_EQ(::write(c, part.data(), part.size()), static_cast<ssize_t>(part.size()));
  }
  ::shutdown(c, SHUT_WR);  // "I'm done sending"; the echo server then finishes
  server.join();
  reader.join();
  ::close(c);
  EXPECT_EQ(got, msg);
  EXPECT_EQ(echoed, msg.size());
}

}  // namespace
}  // namespace rd
