#include "repl/replica/replica.h"

#include <sys/socket.h>
#include <unistd.h>

#include "net/nonblocking/nonblocking.h"
#include "persist/rdb_reader/rdb_reader.h"
#include "resp/encoder/encoder.h"
#include "resp/parser/parser.h"

namespace rd {

Replica::~Replica() {
  // TODO(you): stop().
}

void Replica::start(std::string host, uint16_t port) {
  // TODO(you): remember the primary, mark active, connect().
}

void Replica::stop() {
  // TODO(you): cancel timers, remove loop events, close, state kNone.
}

void Replica::connect() {
  // TODO(you): connect_nonblocking; watch for writable -> on_connected.
}

void Replica::on_connected() {
  // TODO(you): connect_error; then watch for readable, send PING, state kHandshake.
}

void Replica::on_readable() {
  // TODO(you): read_available into in_; then, by state: handshake lines, the RDB transfer,
  // or the stream. EOF/errors -> disconnect_and_retry().
}

void Replica::send_request(std::initializer_list<std::string> args) {
  // TODO(you): encode as RESP and write (a small blocking write is fine here).
}

void Replica::disconnect_and_retry() {
  // TODO(you): close, state kConnecting, a 100 ms timer that calls connect() if active.
}

void register_replicaof_command(CommandTable& table, Replica& replica) {
  // TODO(you): REPLICAOF and SLAVEOF.
}

}  // namespace rd
