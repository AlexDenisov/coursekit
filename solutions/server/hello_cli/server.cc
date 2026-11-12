#include "server/hello_cli/server.h"

#include <string>

#include "resp/encoder/encoder.h"
#include "resp/parser/parser.h"

namespace rd {

void BlockingServer::serve_one() {
  Fd client = accept_connection(listener_.get());
  if (!client.valid()) return;
  std::string input;
  char buf[16384];
  for (;;) {
    ssize_t n = read_some(client.get(), buf, sizeof buf);
    if (n <= 0) return;
    input.append(buf, static_cast<size_t>(n));
    std::string replies;
    bool close_after = false;
    size_t pos = 0;
    for (;;) {
      resp::ParseResult r = resp::parse_request(std::string_view(input).substr(pos));
      if (r.status == resp::ParseStatus::kIncomplete) break;
      if (r.status == resp::ParseStatus::kError) {
        resp::append_error(replies, r.error);
        close_after = true;
        break;
      }
      pos += r.consumed;
      if (!r.args.empty()) table_.dispatch(r.args, replies);
    }
    input.erase(0, pos);
    if (!replies.empty() && !write_all(client.get(), replies)) return;
    if (close_after) return;
  }
}

}  // namespace rd
