#include "server/durable_redis/durable_redis.h"

#include <unistd.h>

#include "persist/rdb_reader/rdb_reader.h"

namespace rd {

DurableRedis::DurableRedis(const Clock& clock, DurableRedisOptions options)
    : clock_(clock),
      options_(options),
      base_(clock, options.base),
      aof_(options.dir + "/appendonly.aof", options.appendfsync, clock),
      saver_(base_.keyspace(), base_.server().loop(), options.dir + "/dump.rdb"),
      rewriter_(base_.keyspace(), aof_, base_.server().loop()),
      replica_(base_.keyspace(), base_.table(), base_.server().loop()) {
  // TODO(you): register the save, rewrite and replicaof commands on base_.table(); a
  // new-client hook whose runner logs writes (if appendonly) and flushes the AOF; the
  // 100 ms aof.cron() timer.
}

bool DurableRedis::load() {
  // TODO(you): AOF first (load_aof_file into base_.table()), else the RDB
  // (rdb::load_snapshot_file into base_.keyspace()); then aof_.open() if appendonly.
  return false;
}

uint16_t DurableRedis::listen(std::string_view host, uint16_t port) {
  // Given.
  return base_.listen(host, port);
}

}  // namespace rd
