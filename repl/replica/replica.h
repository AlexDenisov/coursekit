#pragma once

// repl/replica: following a primary (REPLICAOF). The spec is this file; the story is
// README.md.

#include <cstddef>
#include <cstdint>
#include <string>

#include "cmd/dispatch/dispatch.h"
#include "db/keyspace/keyspace.h"
#include "net/buffers/buffer.h"
#include "net/event_loop/event_loop.h"
#include "net/sockets/sockets.h"

namespace rd {

class Replica {
 public:
  enum class State {
    kNone,        // not replicating
    kConnecting,  // waiting for the TCP connection (or for the retry timer)
    kHandshake,   // PING / REPLCONF / PSYNC exchange
    kTransfer,    // receiving the RDB of a full resync
    kStreaming,   // applying the live command stream
  };

  // Writes go into ks through table. listening_port is what REPLCONF listening-port
  // announces.
  Replica(Keyspace& ks, const CommandTable& table, EventLoop& loop, uint16_t listening_port = 0)
      : ks_(ks), table_(table), loop_(loop), listening_port_(listening_port) {}
  ~Replica();

  // REPLICAOF host port. Everything below happens in loop callbacks, never blocking:
  //  1. connect (net/nonblocking's connect_nonblocking; done when writable).
  //  2. handshake, one request at a time, waiting for each reply line:
  //       PING                                    -> +PONG (or any -error: give up, retry)
  //       REPLCONF listening-port <port>          -> +OK
  //       REPLCONF capa psync2                    -> +OK
  //       PSYNC <replid> <offset + 1>  if a primary was followed before, else PSYNC ? -1
  //  3. "+FULLRESYNC <replid> <offset>": read "$<len>\r\n" (skipping any "\n" keepalives
  //     before it) then exactly len bytes of RDB (no CRLF after them); clear the keyspace,
  //     load it (persist/rdb_reader), adopt replid and offset; full_syncs() + 1.
  //     "+CONTINUE [<replid>]": keep the data, adopt the replid if one is given;
  //     partial_syncs() + 1. Anything else: disconnect and retry.
  //  4. streaming: run each complete request from the primary with table.dispatch,
  //     discarding the replies, and add its size in bytes to offset(). Except:
  //       SELECT <db>           ignored (only db 0 exists)
  //       REPLCONF GETACK *     reply "REPLCONF ACK <offset>" to the primary right away
  //                             (the offset before this request)
  //     Every second while streaming, send "REPLCONF ACK <offset>" (a timer).
  //  On EOF or an error anywhere: close, and retry from step 1 after 100 ms (a timer).
  void start(std::string host, uint16_t port);

  // REPLICAOF NO ONE: disconnect and stop retrying. Keeps the data, replid and offset.
  void stop();

  State state() const { return state_; }
  const std::string& replid() const { return replid_; }
  uint64_t offset() const { return offset_; }
  size_t full_syncs() const { return full_syncs_; }
  size_t partial_syncs() const { return partial_syncs_; }

 private:
  // Suggested helpers (implement in replica.cc, add more as you like).
  void connect();
  void on_connected();
  void on_readable();
  void send_request(std::initializer_list<std::string> args);
  void disconnect_and_retry();

  Keyspace& ks_;
  const CommandTable& table_;
  EventLoop& loop_;
  uint16_t listening_port_;
  std::string host_;
  uint16_t port_ = 0;
  bool active_ = false;
  State state_ = State::kNone;
  int handshake_step_ = 0;
  Fd fd_;
  Buffer in_;
  int64_t rdb_left_ = -1;  // bytes of RDB still expected, -1 if the "$len" line wasn't read yet
  std::string rdb_;
  uint64_t pending_offset_ = 0;  // the offset FULLRESYNC announced
  std::string replid_;
  uint64_t offset_ = 0;
  size_t full_syncs_ = 0;
  size_t partial_syncs_ = 0;
  TimerId ack_timer_ = 0;
  TimerId retry_timer_ = 0;
};

// REPLICAOF host port [3] -> start(), +OK.  REPLICAOF NO ONE -> stop(), +OK. (Also
// registered as SLAVEOF, its old name.)
void register_replicaof_command(CommandTable& table, Replica& replica);

}  // namespace rd
