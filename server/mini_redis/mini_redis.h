#pragma once

// server/mini_redis: the main milestone. The story is README.md.

#include <cstdint>
#include <string_view>

#include "cmd/dispatch/dispatch.h"
#include "common/clock.h"
#include "common/rng.h"
#include "db/keyspace/keyspace.h"
#include "net/client/client.h"
#include "server/event_server/event_server.h"

namespace rd {

struct MiniRedisOptions {
  int hz = 10;  // cron runs per second (Redis's `hz`)
  ClientLimits limits;
  uint64_t seed = 1;  // for the Rng (RANDOMKEY, SPOP, active expiry)
};

class MiniRedis {
 public:
  // Registers every command on table(): connection (PING, ECHO), keyspace, expire, lists,
  // hashes, sets, generic, and the optional ones (lists, sets); and schedules the cron.
  MiniRedis(const Clock& clock, MiniRedisOptions options = {});

  uint16_t listen(std::string_view host, uint16_t port) { return server_.listen(host, port); }
  void run() { server_.run(); }
  void stop() { server_.stop(); }  // thread-safe

  Keyspace& keyspace() { return ks_; }
  CommandTable& table() { return table_; }
  EventServer& server() { return server_; }

  // What the cron does every 1000 / hz ms (serverCron's databasesCron): one
  // active_expire_cycle, then up to 100 rehash steps on each of the keyspace's two dicts
  // (entries and expires) if they're rehashing. Public so tests can call it.
  void cron();

 private:
  const Clock& clock_;
  MiniRedisOptions options_;
  Rng rng_;
  Keyspace ks_;
  CommandTable table_;
  EventServer server_;
};

}  // namespace rd
