#include "net/nonblocking/nonblocking.h"

#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>

#include <cerrno>
#include <string>

namespace rd {

bool set_nonblocking(int fd) {
  // TODO(you): fcntl F_GETFL, add O_NONBLOCK, F_SETFL.
  return false;
}

IoResult read_available(int fd, Buffer& buf, size_t max) {
  // TODO(you): loop: read into buf.prepare(...), commit what you got. 0 bytes means
  // eof; -1 with EAGAIN/EWOULDBLOCK means "that's all for now".
  return {};
}

IoResult write_available(int fd, Buffer& buf) {
  // TODO(you): loop while buf isn't empty: write(view), consume what was taken.
  return {};
}

Fd connect_nonblocking(std::string_view host, uint16_t port) {
  // TODO(you): socket, set_nonblocking, connect; EINPROGRESS is expected.
  return Fd();
}

int connect_error(int fd) {
  // TODO(you): getsockopt(fd, SOL_SOCKET, SO_ERROR, ...).
  return -1;
}

}  // namespace rd
