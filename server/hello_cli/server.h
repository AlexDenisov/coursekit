#pragma once

// server/hello_cli: a blocking, one-client-at-a-time server. The story is README.md.

#include <cstdint>
#include <string_view>

#include "cmd/dispatch/dispatch.h"
#include "net/sockets/sockets.h"

namespace rd {

class BlockingServer {
 public:
  explicit BlockingServer(const CommandTable& table) : table_(table) {}

  // Starts listening on host:port (port 0 = any free port). Returns the port, or 0 on
  // error. Given.
  uint16_t listen(std::string_view host, uint16_t port) {
    listener_ = listen_tcp(host, port);
    return listener_.valid() ? local_port(listener_.get()) : 0;
  }

  // Accepts one client and serves it until it disconnects:
  //   - read whatever has arrived (any amount), append it to the client's input;
  //   - run every complete request in the input, in order, with table.dispatch,
  //     skipping empty requests; collect all their replies;
  //   - write the collected replies with one write_all, then read again.
  // A protocol error: send the replies collected so far followed by the error, then
  // close. Returns when the connection is closed (by either side, or a write error).
  void serve_one();

  // serve_one, forever.
  void run() {
    for (;;) serve_one();
  }

 private:
  const CommandTable& table_;
  Fd listener_;
};

}  // namespace rd
