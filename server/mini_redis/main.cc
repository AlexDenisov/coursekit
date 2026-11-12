// The server binary. Given: everything it runs is yours.
//
//   bazel run -c opt //server/mini_redis -- 6380
//   redis-cli -p 6380

#include <csignal>
#include <cstdio>
#include <cstdlib>

#include "server/mini_redis/mini_redis.h"

int main(int argc, char** argv) {
  std::signal(SIGPIPE, SIG_IGN);
  int port = argc > 1 ? std::atoi(argv[1]) : 6380;
  rd::SystemClock clock;
  rd::MiniRedis server(clock);
  if (server.listen("127.0.0.1", static_cast<uint16_t>(port)) == 0) {
    std::perror("listen");
    return 1;
  }
  std::printf("mini_redis: listening on 127.0.0.1:%d\n", port);
  std::fflush(stdout);
  server.run();
}
