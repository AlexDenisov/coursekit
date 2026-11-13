#pragma once

// server/durable_redis: Mini Redis that survives restarts and follows a primary. The
// story is README.md.

#include <cstdint>
#include <string>
#include <string_view>

#include "common/clock.h"
#include "persist/aof/aof.h"
#include "persist/aof_rewrite/aof_rewrite.h"
#include "persist/bgsave/bgsave.h"
#include "repl/replica/replica.h"
#include "server/mini_redis/mini_redis.h"

namespace rd {

struct DurableRedisOptions {
  MiniRedisOptions base;
  std::string dir = ".";  // dump.rdb and appendonly.aof live here
  bool appendonly = true;
  FsyncPolicy appendfsync = FsyncPolicy::kEverySec;
};

class DurableRedis {
 public:
  // Everything MiniRedis has, plus: SAVE, BGSAVE, LASTSAVE (persist/bgsave),
  // BGREWRITEAOF (persist/aof_rewrite), REPLICAOF/SLAVEOF (repl/replica). With appendonly,
  // every client's writes are logged (dispatch_logged) and the AOF is flushed right after
  // each command, before its reply can be sent; a timer calls aof.cron() every 100 ms
  // (everysec fsyncs).
  DurableRedis(const Clock& clock, DurableRedisOptions options);

  // Startup, before listen(): with appendonly and an existing appendonly.aof, replay it
  // (Redis prefers the AOF: it's more recent); else load dump.rdb if it exists. Then open
  // the AOF for appending. False if a file exists but can't be loaded.
  bool load();

  uint16_t listen(std::string_view host, uint16_t port);
  void run() { base_.run(); }
  void stop() { base_.stop(); }

  MiniRedis& base() { return base_; }
  Replica& replica() { return replica_; }
  Aof& aof() { return aof_; }

 private:
  const Clock& clock_;
  DurableRedisOptions options_;
  MiniRedis base_;
  Aof aof_;
  BackgroundSaver saver_;
  AofRewriter rewriter_;
  Replica replica_;
};

}  // namespace rd
