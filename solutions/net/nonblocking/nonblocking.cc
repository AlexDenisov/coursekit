#include "net/nonblocking/nonblocking.h"

#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>

#include <algorithm>
#include <cerrno>
#include <string>

namespace rd {

bool set_nonblocking(int fd) {
  int flags = ::fcntl(fd, F_GETFL);
  return flags >= 0 && ::fcntl(fd, F_SETFL, flags | O_NONBLOCK) == 0;
}

IoResult read_available(int fd, Buffer& buf, size_t max) {
  IoResult r;
  while (r.bytes < max) {
    size_t want = std::min<size_t>(16384, max - r.bytes);
    char* p = buf.prepare(want);
    ssize_t n = ::read(fd, p, want);
    if (n > 0) {
      buf.commit(static_cast<size_t>(n));
      r.bytes += static_cast<size_t>(n);
    } else if (n == 0) {
      r.eof = true;
      break;
    } else if (errno == EINTR) {
      continue;
    } else if (errno == EAGAIN || errno == EWOULDBLOCK) {
      break;
    } else {
      r.error = true;
      r.err = errno;
      break;
    }
  }
  return r;
}

IoResult write_available(int fd, Buffer& buf) {
  IoResult r;
  int one = 1;
  ::setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof one);
  while (!buf.empty()) {
    std::string_view v = buf.view();
    ssize_t n = ::write(fd, v.data(), v.size());
    if (n >= 0) {
      buf.consume(static_cast<size_t>(n));
      r.bytes += static_cast<size_t>(n);
    } else if (errno == EINTR) {
      continue;
    } else if (errno == EAGAIN || errno == EWOULDBLOCK) {
      break;
    } else {
      r.error = true;
      r.err = errno;
      break;
    }
  }
  return r;
}

Fd connect_nonblocking(std::string_view host, uint16_t port) {
  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_port = htons(port);
  if (::inet_pton(AF_INET, std::string(host).c_str(), &addr.sin_addr) != 1) return Fd();
  Fd fd(::socket(AF_INET, SOCK_STREAM, 0));
  if (!fd.valid() || !set_nonblocking(fd.get())) return Fd();
  if (::connect(fd.get(), reinterpret_cast<sockaddr*>(&addr), sizeof addr) != 0 &&
      errno != EINPROGRESS) {
    return Fd();
  }
  return fd;
}

int connect_error(int fd) {
  int err = 0;
  socklen_t len = sizeof err;
  if (::getsockopt(fd, SOL_SOCKET, SO_ERROR, &err, &len) != 0) return errno;
  return err;
}

}  // namespace rd
