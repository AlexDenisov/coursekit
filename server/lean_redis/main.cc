// The server binary. Given.
//
//   bazel run -c opt //server/lean_redis -- 6380 [maxmemory-bytes] [policy]
//   redis-cli -p 6380 CONFIG SET maxmemory 100000000

#include <csignal>
#include <cstdio>
#include <cstdlib>

#include "server/lean_redis/lean_redis.h"

int main(int argc, char** argv) {
  std::signal(SIGPIPE, SIG_IGN);
  int port = argc > 1 ? std::atoi(argv[1]) : 6380;
  rd::LeanRedisOptions options;
  if (argc > 2) options.eviction.maxmemory = std::strtoull(argv[2], nullptr, 10);
  options.eviction.policy = rd::EvictionPolicy::kAllKeysLru;
  if (argc > 3) {
    if (auto p = rd::parse_policy(argv[3])) options.eviction.policy = *p;
  }
  rd::SystemClock clock;
  rd::LeanRedis server(clock, options);
  if (server.listen("127.0.0.1", static_cast<uint16_t>(port)) == 0) {
    std::perror("listen");
    return 1;
  }
  std::printf("lean_redis: listening on 127.0.0.1:%d\n", port);
  std::fflush(stdout);
  server.run();
}
