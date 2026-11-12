#include "net/nonblocking/nonblocking.h"

#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <gtest/gtest.h>

#include <cerrno>
#include <string>

namespace rd {
namespace {

// A connected pair of local sockets; both ends blocking until a test says otherwise.
struct Pair {
  Pair() {
    int fds[2];
    EXPECT_EQ(::socketpair(AF_UNIX, SOCK_STREAM, 0, fds), 0);
    a = fds[0];
    b = fds[1];
    int one = 1;
    ::setsockopt(a, SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof one);
    ::setsockopt(b, SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof one);
  }
  ~Pair() {
    if (a >= 0) ::close(a);
    if (b >= 0) ::close(b);
  }
  int a = -1, b = -1;
};

bool is_nonblocking(int fd) { return (::fcntl(fd, F_GETFL) & O_NONBLOCK) != 0; }

TEST(Nonblocking, SetNonblockingKeepsOtherFlags) {
  Pair p;
  ::fcntl(p.a, F_SETFL, ::fcntl(p.a, F_GETFL) | O_APPEND);
  ASSERT_TRUE(set_nonblocking(p.a));
  EXPECT_TRUE(is_nonblocking(p.a));
  EXPECT_TRUE(::fcntl(p.a, F_GETFL) & O_APPEND);
}

TEST(Nonblocking, ReadWithNothingAvailableReturnsAtOnce) {
  Pair p;
  ASSERT_TRUE(set_nonblocking(p.a));
  ASSERT_TRUE(is_nonblocking(p.a)) << "a blocking read would hang this test";
  Buffer buf;
  IoResult r = read_available(p.a, buf);
  EXPECT_EQ(r.bytes, 0u);
  EXPECT_FALSE(r.eof);
  EXPECT_FALSE(r.error);
}

// Several writes are waiting; one call reads all of them (until EAGAIN), not just one.
TEST(Nonblocking, ReadsEverythingAvailable) {
  Pair p;
  ASSERT_TRUE(set_nonblocking(p.a));
  ASSERT_TRUE(is_nonblocking(p.a));
  std::string sent;
  for (int i = 0; i < 5; ++i) {
    std::string part(1000, static_cast<char>('a' + i));
    ASSERT_EQ(::write(p.b, part.data(), part.size()), 1000);
    sent += part;
  }
  Buffer buf;
  buf.append("old:");
  IoResult r = read_available(p.a, buf);
  EXPECT_EQ(r.bytes, 5000u);
  EXPECT_FALSE(r.eof);
  EXPECT_EQ(buf.view(), "old:" + sent);
}

TEST(Nonblocking, ReadStopsAtMax) {
  Pair p;
  ASSERT_TRUE(set_nonblocking(p.a));
  ASSERT_TRUE(is_nonblocking(p.a));
  std::string data(3000, 'x');
  ASSERT_EQ(::write(p.b, data.data(), data.size()), 3000);
  Buffer buf;
  EXPECT_EQ(read_available(p.a, buf, 1000).bytes, 1000u);
  EXPECT_EQ(read_available(p.a, buf).bytes, 2000u);
}

TEST(Nonblocking, ReadSeesEof) {
  Pair p;
  ASSERT_TRUE(set_nonblocking(p.a));
  ASSERT_TRUE(is_nonblocking(p.a));
  ASSERT_EQ(::write(p.b, "bye", 3), 3);
  ::close(p.b);
  p.b = -1;
  Buffer buf;
  IoResult r = read_available(p.a, buf);
  EXPECT_EQ(r.bytes, 3u);
  EXPECT_TRUE(r.eof);
  EXPECT_EQ(buf.view(), "bye");
}

// The pitfall: a non-blocking write takes only what fits in the kernel's buffer. The
// rest must stay queued, in order, for when the socket is writable again.
TEST(Nonblocking, PartialWritesKeepTheRest) {
  Pair p;
  ASSERT_TRUE(set_nonblocking(p.a));
  ASSERT_TRUE(is_nonblocking(p.a));
  std::string data(2 << 20, '\0');
  for (size_t i = 0; i < data.size(); ++i) data[i] = static_cast<char>(i % 251);
  Buffer out;
  out.append(data);
  IoResult first = write_available(p.a, out);
  EXPECT_FALSE(first.error);
  EXPECT_GT(first.bytes, 0u);
  EXPECT_LT(first.bytes, data.size()) << "2 MB fit in a socket buffer?";
  EXPECT_EQ(out.size(), data.size() - first.bytes);

  std::string got;
  char tmp[65536];
  while (got.size() < data.size()) {
    ssize_t n = ::read(p.b, tmp, sizeof tmp);  // the reader drains...
    ASSERT_GT(n, 0);
    got.append(tmp, static_cast<size_t>(n));
    if (!out.empty()) ASSERT_FALSE(write_available(p.a, out).error);  // ...the writer refills
  }
  EXPECT_TRUE(out.empty());
  EXPECT_TRUE(got == data) << "bytes lost or reordered";
}

TEST(Nonblocking, WriteToClosedPeerIsAnErrorNotASignal) {
  Pair p;
  ASSERT_TRUE(set_nonblocking(p.a));
  ::close(p.b);
  p.b = -1;
  Buffer out;
  out.append("hello?");
  IoResult r = write_available(p.a, out);
  EXPECT_TRUE(r.error);
  EXPECT_EQ(r.err, EPIPE);
}

TEST(Nonblocking, ConnectCompletesWhenWritable) {
  Fd l = listen_tcp("127.0.0.1", 0);
  ASSERT_TRUE(l.valid());
  Fd c = connect_nonblocking("127.0.0.1", local_port(l.get()));
  ASSERT_TRUE(c.valid());
  EXPECT_TRUE(is_nonblocking(c.get()));
  pollfd pfd{c.get(), POLLOUT, 0};
  ASSERT_EQ(::poll(&pfd, 1, 2000), 1);
  EXPECT_EQ(connect_error(c.get()), 0);
}

TEST(Nonblocking, ConnectToClosedPortReportsTheError) {
  uint16_t port;
  {
    Fd l = listen_tcp("127.0.0.1", 0);
    port = local_port(l.get());
  }
  Fd c = connect_nonblocking("127.0.0.1", port);
  if (!c.valid()) {
    SUCCEED() << "refused immediately, which is allowed";
    return;
  }
  pollfd pfd{c.get(), POLLOUT, 0};
  ASSERT_EQ(::poll(&pfd, 1, 2000), 1);
  EXPECT_EQ(connect_error(c.get()), ECONNREFUSED);
}

}  // namespace
}  // namespace rd
