#include "server/event_server/event_server.h"

#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>

#include "net/nonblocking/nonblocking.h"

namespace rd {

uint16_t EventServer::listen(std::string_view host, uint16_t port) {
  // TODO(you): listen_tcp, set_nonblocking, loop_.add(listener, kReadable, ...accept_all),
  // loop_.set_before_sleep(...flush_all). Return the port.
  return 0;
}

void EventServer::accept_all() {
  // TODO(you): ::accept until it fails (EAGAIN: no more pending); for each: non-blocking,
  // TCP_NODELAY, a Client in clients_, the hook, loop_.add(fd, kReadable, ...handle_read).
}

void EventServer::handle_read(int fd) {
  // TODO(you): find the client; on_readable; close it or flush it.
}

void EventServer::handle_write(int fd) {
  // TODO(you): on_writable; stop watching kWritable when done; close if it should.
}

void EventServer::flush(Client& c) {
  // TODO(you): if it wants to write: on_writable now; still wants to? watch kWritable.
}

void EventServer::flush_all() {
  // TODO(you): flush every client that wants to write (and close the ones that should).
}

void EventServer::close_client(int fd) {
  // TODO(you): loop_.remove(fd, both), the close hook, then erase from clients_.
}

}  // namespace rd
