#include "server/hello_cli/server.h"

#include <string>

#include "resp/encoder/encoder.h"
#include "resp/parser/parser.h"

namespace rd {

void BlockingServer::serve_one() {
  // TODO(you): accept_connection; then loop: read_some into a 16 KB buffer, append to
  // `input`, parse_request until kIncomplete (erase what each request consumed), and
  // write_all the replies. Stop on EOF, a write error or a protocol error.
}

}  // namespace rd
