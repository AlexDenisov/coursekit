#pragma once

// net/client: one connection's state. The spec is this file; the story is README.md.

#include <cstddef>
#include <functional>
#include <span>
#include <string>

#include "cmd/dispatch/dispatch.h"
#include "net/buffers/buffer.h"
#include "net/sockets/sockets.h"

namespace rd {

struct ClientLimits {
  // Redis's client-query-buffer-limit: more unparsed input than this closes the client.
  size_t max_query_buffer = size_t{1} << 30;
  // Bytes read per readable event (Redis's PROTO_IOBUF_LEN), so one busy client can't
  // starve the others.
  size_t read_chunk = 16 * 1024;
};

class Client : public Session {
 public:
  // How one parsed request is run. The default is table.dispatch(args, reply, &client).
  // Servers that intercept commands (transactions, pub/sub) install their own.
  using Runner = std::function<void(Client& client, std::span<const std::string> args,
                                    std::string& reply)>;

  // Takes ownership of a connected, non-blocking socket.
  Client(Fd fd, const CommandTable& table, ClientLimits limits = {});

  int fd() const { return fd_.get(); }

  // Call when the socket is readable. Reads once, at most limits.read_chunk bytes, into
  // the query buffer; then process_input(). End of stream or a read error: should_close()
  // becomes true at once (pending replies are dropped). Returns the bytes read.
  size_t on_readable();

  // Runs every complete request in the query buffer, in order, appending each reply to
  // the reply buffer; skips empty requests. Stops early while blocked() or once a
  // protocol error was seen. A protocol error appends its error reply and closes the
  // client after the replies are sent; nothing after it runs. Afterwards, if more than
  // limits.max_query_buffer unparsed bytes remain, closes at once, without a reply.
  void process_input();

  // Call when the socket is writable: writes as much of the reply buffer as it takes.
  // A write error closes at once.
  void on_writable();

  bool wants_write() const { return !replies_.empty() && !closed_; }
  size_t reply_bytes() const { return replies_.size(); }

  // True once the connection should be torn down: EOF, an I/O error, the query limit,
  // close_after_reply() with everything sent.
  bool should_close() const;

  // Close once all pending replies have been written.
  void close_after_reply() { close_after_reply_ = true; }

  // Session: appends bytes to the reply buffer (pub/sub messages, ...).
  void push(std::string_view bytes) override { replies_.append(bytes); }

  // While blocked, process_input() runs nothing; the input waits. (msg/blocking: a
  // client in BLPOP must not run its next request before BLPOP has its reply.)
  void block() { blocked_ = true; }
  void unblock() { blocked_ = false; }
  bool blocked() const { return blocked_; }

  void set_runner(Runner runner) { runner_ = std::move(runner); }

 private:
  Fd fd_;
  const CommandTable& table_;
  ClientLimits limits_;
  Runner runner_;
  Buffer query_;
  Buffer replies_;
  bool closed_ = false;
  bool close_after_reply_ = false;
  bool protocol_error_ = false;
  bool blocked_ = false;
};

}  // namespace rd
