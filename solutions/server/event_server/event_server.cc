#include "server/event_server/event_server.h"

#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>

#include <vector>

#include "net/nonblocking/nonblocking.h"

namespace rd {

uint16_t EventServer::listen(std::string_view host, uint16_t port) {
  listener_ = listen_tcp(host, port);
  if (!listener_.valid() || !set_nonblocking(listener_.get())) return 0;
  loop_.add(listener_.get(), kReadable, [this](int, int) { accept_all(); });
  loop_.set_before_sleep([this] { flush_all(); });
  return local_port(listener_.get());
}

void EventServer::accept_all() {
  for (;;) {
    int fd = ::accept(listener_.get(), nullptr, nullptr);
    if (fd < 0) return;  // EAGAIN (no more), or an error for that one connection
    Fd owned(fd);
    set_nonblocking(fd);
    int one = 1;
    ::setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof one);
    ::setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof one);
    auto client = std::make_unique<Client>(std::move(owned), table_, limits_);
    Client& c = *client;
    clients_[fd] = std::move(client);
    if (new_client_hook_) new_client_hook_(c);
    loop_.add(fd, kReadable, [this](int fd, int) { handle_read(fd); });
  }
}

void EventServer::handle_read(int fd) {
  auto it = clients_.find(fd);
  if (it == clients_.end()) return;
  Client& c = *it->second;
  c.on_readable();
  if (c.should_close()) {
    close_client(fd);
    return;
  }
  flush(c);
}

void EventServer::handle_write(int fd) {
  auto it = clients_.find(fd);
  if (it == clients_.end()) return;
  Client& c = *it->second;
  c.on_writable();
  if (!c.wants_write()) loop_.remove(fd, kWritable);
  if (c.should_close()) close_client(fd);
}

void EventServer::flush(Client& c) {
  if (!c.wants_write()) {
    if (c.should_close()) close_client(c.fd());
    return;
  }
  if (!(loop_.mask(c.fd()) & kWritable)) c.on_writable();
  if (c.should_close()) {
    close_client(c.fd());
    return;
  }
  if (c.wants_write() && !(loop_.mask(c.fd()) & kWritable)) {
    loop_.add(c.fd(), kWritable, [this](int fd, int) { handle_write(fd); });
  }
}

void EventServer::flush_all() {
  std::vector<Client*> pending;
  for (auto& [fd, c] : clients_) {
    if (c->wants_write() || c->should_close()) pending.push_back(c.get());
  }
  for (Client* c : pending) flush(*c);
}

void EventServer::close_client(int fd) {
  loop_.remove(fd, kReadable | kWritable);
  clients_.erase(fd);
}

}  // namespace rd
