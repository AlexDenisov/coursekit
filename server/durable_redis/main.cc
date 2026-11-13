// The server binary. Given.
//
//   mkdir -p /tmp/rd && bazel run -c opt //server/durable_redis -- 6380 /tmp/rd
//   redis-cli -p 6380 SET k v; kill -9 <pid>; start it again; redis-cli -p 6380 GET k

#include <csignal>
#include <cstdio>
#include <cstdlib>

#include "server/durable_redis/durable_redis.h"

int main(int argc, char** argv) {
  std::signal(SIGPIPE, SIG_IGN);
  int port = argc > 1 ? std::atoi(argv[1]) : 6380;
  rd::DurableRedisOptions options;
  options.dir = argc > 2 ? argv[2] : ".";
  rd::SystemClock clock;
  rd::DurableRedis server(clock, options);
  if (!server.load()) {
    std::fprintf(stderr, "durable_redis: can't load the data in %s\n", options.dir.c_str());
    return 1;
  }
  if (server.listen("127.0.0.1", static_cast<uint16_t>(port)) == 0) {
    std::perror("listen");
    return 1;
  }
  std::printf("durable_redis: %zu keys loaded from %s; listening on 127.0.0.1:%d\n",
              server.base().keyspace().size(), options.dir.c_str(), port);
  std::fflush(stdout);
  server.run();
}
