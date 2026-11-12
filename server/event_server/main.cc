// The server binary. Given: everything it runs is yours.
//
//   bazel run -c opt //server/event_server -- 6380
//   redis-benchmark -p 6380 -c 50 -n 100000 -P 16 -t ping_mbulk,set,get -q

#include <csignal>
#include <cstdio>
#include <cstdlib>

#include "db/strings/strings.h"
#include "server/event_server/event_server.h"

int main(int argc, char** argv) {
  std::signal(SIGPIPE, SIG_IGN);
  int port = argc > 1 ? std::atoi(argv[1]) : 6380;
  rd::SystemClock clock;
  rd::StringDb db;
  rd::CommandTable table;
  rd::register_connection_commands(table);
  rd::register_string_commands(table, db);
  rd::EventServer server(table, clock);
  if (server.listen("127.0.0.1", static_cast<uint16_t>(port)) == 0) {
    std::perror("listen");
    return 1;
  }
  std::printf("event_server: listening on 127.0.0.1:%d\n", port);
  std::fflush(stdout);
  server.run();
}
