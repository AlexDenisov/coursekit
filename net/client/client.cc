#include "net/client/client.h"

#include "net/nonblocking/nonblocking.h"
#include "resp/encoder/encoder.h"
#include "resp/parser/parser.h"

namespace rd {

Client::Client(Fd fd, const CommandTable& table, ClientLimits limits)
    : fd_(std::move(fd)), table_(table), limits_(limits) {
  runner_ = [](Client& c, std::span<const std::string> args, std::string& reply) {
    c.table_.dispatch(args, reply, &c);
  };
}

size_t Client::on_readable() {
  // TODO(you): read_available with max = read_chunk; eof/error -> closed_; else
  // process_input().
  return 0;
}

void Client::process_input() {
  // TODO(you): parse_request on query_.view() until kIncomplete; run each request into
  // a std::string, append it to replies_; consume what was parsed. Then the query limit.
}

void Client::on_writable() {
  // TODO(you): write_available; an error closes.
}

bool Client::should_close() const {
  // TODO(you): closed, or asked to close and nothing left to send.
  return false;
}

}  // namespace rd
