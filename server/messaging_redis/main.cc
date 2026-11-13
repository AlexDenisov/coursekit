// The server binary. Given.
//
//   bazel run -c opt //server/messaging_redis -- 6380
//   redis-cli -p 6380 SUBSCRIBE chat            # terminal 1
//   redis-cli -p 6380 PUBLISH chat hello        # terminal 2

#include <csignal>
#include <cstdio>
#include <cstdlib>

#include "server/messaging_redis/messaging_redis.h"

int main(int argc, char** argv) {
  std::signal(SIGPIPE, SIG_IGN);
  int port = argc > 1 ? std::atoi(argv[1]) : 6380;
  rd::SystemClock clock;
  rd::MessagingRedis server(clock);
  if (server.listen("127.0.0.1", static_cast<uint16_t>(port)) == 0) {
    std::perror("listen");
    return 1;
  }
  std::printf("messaging_redis: listening on 127.0.0.1:%d\n", port);
  std::fflush(stdout);
  server.run();
}
