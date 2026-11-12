#pragma once

// net/sockets: blocking TCP. The spec is this file; the story is README.md.

#include <sys/types.h>
#include <unistd.h>

#include <cstddef>
#include <cstdint>
#include <string_view>
#include <utility>

namespace rd {

// Owns a file descriptor and closes it when destroyed. Move-only. Given.
class Fd {
 public:
  Fd() = default;
  explicit Fd(int fd) : fd_(fd) {}
  Fd(Fd&& o) noexcept : fd_(std::exchange(o.fd_, -1)) {}
  Fd& operator=(Fd&& o) noexcept {
    if (this != &o) reset(std::exchange(o.fd_, -1));
    return *this;
  }
  Fd(const Fd&) = delete;
  Fd& operator=(const Fd&) = delete;
  ~Fd() { reset(); }

  int get() const { return fd_; }
  bool valid() const { return fd_ >= 0; }
  int release() { return std::exchange(fd_, -1); }
  void reset(int fd = -1) {
    if (fd_ >= 0) ::close(fd_);
    fd_ = fd;
  }

 private:
  int fd_ = -1;
};

// A listening IPv4 TCP socket on host:port ("127.0.0.1", "0.0.0.0"); port 0 asks the
// kernel for any free port. Sets SO_REUSEADDR before bind() so a restarted server can
// reuse its port at once. Invalid Fd on any error (errno says why).
Fd listen_tcp(std::string_view host, uint16_t port, int backlog = 511);

// The port a socket is bound to (useful after listening on port 0). 0 on error.
uint16_t local_port(int fd);

// Waits for and accepts one connection. Retries on EINTR. Invalid Fd on error.
Fd accept_connection(int listen_fd);

// A connected TCP socket to host:port. Invalid Fd on error.
Fd connect_tcp(std::string_view host, uint16_t port);

// Reads up to n bytes: the count (> 0), 0 at end of stream, -1 on error. Retries on
// EINTR. Returns as soon as any bytes are available: a short read is normal.
ssize_t read_some(int fd, char* buf, size_t n);

// Writes all of data, however many write() calls it takes. Retries on EINTR. Returns
// false on error, including a peer that closed the connection; must never let the
// process die of SIGPIPE.
bool write_all(int fd, std::string_view data);

// An echo server for one client: accepts one connection on listen_fd and writes back
// every byte it reads until the client closes its side. Returns the number of bytes
// echoed.
size_t echo_one_client(int listen_fd);

}  // namespace rd
