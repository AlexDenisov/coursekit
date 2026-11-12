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
  if (closed_) return 0;
  IoResult r = read_available(fd_.get(), query_, limits_.read_chunk);
  if (r.eof || r.error) {
    closed_ = true;
    return r.bytes;
  }
  process_input();
  return r.bytes;
}

void Client::process_input() {
  while (!blocked_ && !protocol_error_ && !closed_) {
    resp::ParseResult r = resp::parse_request(query_.view());
    if (r.status == resp::ParseStatus::kIncomplete) break;
    if (r.status == resp::ParseStatus::kError) {
      std::string err;
      resp::append_error(err, r.error);
      replies_.append(err);
      protocol_error_ = true;
      close_after_reply_ = true;
      query_.clear();
      return;
    }
    query_.consume(r.consumed);
    if (r.args.empty()) continue;
    std::string reply;
    runner_(*this, r.args, reply);
    replies_.append(reply);
  }
  if (query_.size() > limits_.max_query_buffer) closed_ = true;
}

void Client::on_writable() {
  if (closed_ || replies_.empty()) return;
  IoResult r = write_available(fd_.get(), replies_);
  if (r.error) closed_ = true;
}

bool Client::should_close() const { return closed_ || (close_after_reply_ && replies_.empty()); }

}  // namespace rd
