// The server binary. Given: everything it runs is yours.
//
//   bazel run //server/hello_cli -- 6380        then, in another terminal:
//   redis-cli -p 6380

#include <cstdio>
#include <cstdlib>

#include "cmd/dispatch/dispatch.h"
#include "db/strings/strings.h"
#include "server/hello_cli/server.h"

int main(int argc, char** argv) {
  int port = argc > 1 ? std::atoi(argv[1]) : 6380;
  rd::StringDb db;
  rd::CommandTable table;
  rd::register_connection_commands(table);
  rd::register_string_commands(table, db);
  rd::BlockingServer server(table);
  if (server.listen("127.0.0.1", static_cast<uint16_t>(port)) == 0) {
    std::perror("listen");
    return 1;
  }
  std::printf("hello_cli: listening on 127.0.0.1:%d (one client at a time)\n", port);
  std::fflush(stdout);
  server.run();
}
