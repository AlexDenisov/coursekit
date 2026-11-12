#pragma once

// server/event_server: one thread, many clients. The story is README.md.

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string_view>
#include <unordered_map>

#include "cmd/dispatch/dispatch.h"
#include "common/clock.h"
#include "net/client/client.h"
#include "net/event_loop/event_loop.h"
#include "net/sockets/sockets.h"

namespace rd {

class EventServer {
 public:
  EventServer(const CommandTable& table, const Clock& clock, ClientLimits limits = {})
      : table_(table), limits_(limits), loop_(clock) {}

  // Listens on host:port (0 = any free port), non-blocking, and watches the listener:
  // when readable, accept every pending connection (until EAGAIN), make each
  // non-blocking with TCP_NODELAY, wrap it in a Client, call the new-client hook, and
  // watch it for reading. Returns the port, 0 on error.
  uint16_t listen(std::string_view host, uint16_t port);

  // Per client, in the loop:
  //   readable: on_readable(); then, if it has replies, try on_writable() at once, and
  //             watch it for writing only if some are left; close it if should_close().
  //   writable: on_writable(); stop watching for writing once nothing is left; close it
  //             if should_close().
  //   before sleeping: replies can appear for a client that wasn't the one running
  //             (Session::push); give every such client the same "write now, else watch
  //             for writing" treatment.
  // Closing a client: stop watching it, then destroy it (which closes the socket).

  void run() { loop_.run(); }
  void stop() { loop_.stop(); }  // thread-safe
  EventLoop& loop() { return loop_; }
  size_t client_count() const { return clients_.size(); }

  // Called for every new client before it's served (the Messaging track uses it).
  void set_new_client_hook(std::function<void(Client&)> hook) { new_client_hook_ = std::move(hook); }

 private:
  void accept_all();            // the listener's readable handler
  void handle_read(int fd);     // a client's readable handler
  void handle_write(int fd);    // a client's writable handler
  void flush(Client& c);        // write now; watch for writable if anything is left
  void flush_all();             // before sleeping: flush every client with pending replies
  void close_client(int fd);

  const CommandTable& table_;
  ClientLimits limits_;
  EventLoop loop_;
  Fd listener_;
  std::unordered_map<int, std::unique_ptr<Client>> clients_;  // by fd
  std::function<void(Client&)> new_client_hook_;
};

}  // namespace rd
