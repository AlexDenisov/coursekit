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
  register_save_commands(base_.table(), saver_);
  register_rewrite_command(base_.table(), rewriter_);
  register_replicaof_command(base_.table(), replica_);
  base_.server().set_new_client_hook([this](Client& client) {
    client.set_runner([this](Client& c, std::span<const std::string> args, std::string& reply) {
      if (!options_.appendonly) return base_.table().dispatch(args, reply, &c);
      dispatch_logged(base_.table(), base_.keyspace(), aof_, args, reply, &c);
      aof_.flush();
    });
  });
  base_.server().loop().timers().add(clock_.now_ms() + 100, [this](TimerId) {
    aof_.cron();
    return 100;
  });
}

bool DurableRedis::load() {
  std::string aof_path = options_.dir + "/appendonly.aof";
  std::string rdb_path = options_.dir + "/dump.rdb";
  if (options_.appendonly && ::access(aof_path.c_str(), F_OK) == 0) {
    if (!load_aof_file(aof_path, base_.table()).ok) return false;
  } else if (::access(rdb_path.c_str(), F_OK) == 0) {
    if (!rdb::load_snapshot_file(rdb_path, base_.keyspace()).ok) return false;
  }
  return !options_.appendonly || aof_.open();
}

uint16_t DurableRedis::listen(std::string_view host, uint16_t port) { return base_.listen(host, port); }

}  // namespace rd
