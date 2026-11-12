#include "net/sockets/sockets.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>

#include <cerrno>
#include <string>

namespace rd {

namespace {

bool make_addr(std::string_view host, uint16_t port, sockaddr_in* addr) {
  *addr = {};
  addr->sin_family = AF_INET;
  addr->sin_port = htons(port);
  return ::inet_pton(AF_INET, std::string(host).c_str(), &addr->sin_addr) == 1;
}

}  // namespace

Fd listen_tcp(std::string_view host, uint16_t port, int backlog) {
  sockaddr_in addr;
  if (!make_addr(host, port, &addr)) {
    errno = EINVAL;
    return Fd();
  }
  Fd fd(::socket(AF_INET, SOCK_STREAM, 0));
  if (!fd.valid()) return Fd();
  int one = 1;
  if (::setsockopt(fd.get(), SOL_SOCKET, SO_REUSEADDR, &one, sizeof one) != 0) return Fd();
  if (::bind(fd.get(), reinterpret_cast<sockaddr*>(&addr), sizeof addr) != 0) return Fd();
  if (::listen(fd.get(), backlog) != 0) return Fd();
  return fd;
}

uint16_t local_port(int fd) {
  sockaddr_in addr{};
  socklen_t len = sizeof addr;
  if (::getsockname(fd, reinterpret_cast<sockaddr*>(&addr), &len) != 0) return 0;
  return ntohs(addr.sin_port);
}

Fd accept_connection(int listen_fd) {
  for (;;) {
    int fd = ::accept(listen_fd, nullptr, nullptr);
    if (fd >= 0) return Fd(fd);
    if (errno != EINTR) return Fd();
  }
}

Fd connect_tcp(std::string_view host, uint16_t port) {
  sockaddr_in addr;
  if (!make_addr(host, port, &addr)) {
    errno = EINVAL;
    return Fd();
  }
  Fd fd(::socket(AF_INET, SOCK_STREAM, 0));
  if (!fd.valid()) return Fd();
  if (::connect(fd.get(), reinterpret_cast<sockaddr*>(&addr), sizeof addr) != 0) return Fd();
  return fd;
}

ssize_t read_some(int fd, char* buf, size_t n) {
  for (;;) {
    ssize_t r = ::read(fd, buf, n);
    if (r >= 0 || errno != EINTR) return r;
  }
}

bool write_all(int fd, std::string_view data) {
  int one = 1;
  ::setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof one);  // fails harmlessly on non-sockets
  while (!data.empty()) {
    ssize_t n = ::write(fd, data.data(), data.size());
    if (n < 0) {
      if (errno == EINTR) continue;
      return false;
    }
    data.remove_prefix(static_cast<size_t>(n));
  }
  return true;
}

size_t echo_one_client(int listen_fd) {
  Fd c = accept_connection(listen_fd);
  if (!c.valid()) return 0;
  size_t total = 0;
  char buf[16384];
  for (;;) {
    ssize_t n = read_some(c.get(), buf, sizeof buf);
    if (n <= 0) break;
    if (!write_all(c.get(), std::string_view(buf, static_cast<size_t>(n)))) break;
    total += static_cast<size_t>(n);
  }
  return total;
}

}  // namespace rd
