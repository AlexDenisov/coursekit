#include "net/sockets/sockets.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>

#include <cerrno>
#include <string>

namespace rd {

Fd listen_tcp(std::string_view host, uint16_t port, int backlog) {
  // TODO(you): socket(AF_INET, SOCK_STREAM), setsockopt SO_REUSEADDR, inet_pton the
  // host into a sockaddr_in (htons the port), bind, listen.
  return Fd();
}

uint16_t local_port(int fd) {
  // TODO(you): getsockname, then ntohs.
  return 0;
}

Fd accept_connection(int listen_fd) {
  // TODO(you): accept, again while it fails with EINTR.
  return Fd();
}

Fd connect_tcp(std::string_view host, uint16_t port) {
  // TODO(you): socket, then connect to the same kind of sockaddr_in as listen_tcp.
  return Fd();
}

ssize_t read_some(int fd, char* buf, size_t n) {
  // TODO(you): read, again on EINTR.
  return -1;
}

bool write_all(int fd, std::string_view data) {
  // TODO(you): loop until everything is written. On macOS, set SO_NOSIGPIPE on the
  // socket (or ignore SIGPIPE) so a closed peer gives EPIPE instead of a dead process.
  return false;
}

size_t echo_one_client(int listen_fd) {
  // TODO(you): accept, then read_some / write_all until read_some returns 0.
  return 0;
}

}  // namespace rd
