#pragma once

// server/lean_redis: Mini Redis plus sorted sets and maxmemory eviction. The story is
// README.md.

#include <cstddef>
#include <cstdint>
#include <string_view>

#include "common/clock.h"
#include "common/rng.h"
#include "db/eviction/eviction.h"
#include "server/mini_redis/mini_redis.h"

namespace rd {

// Bytes currently allocated with malloc in this process (macOS's
// malloc_zone_statistics). About 50 us per call: don't call it on every command. Given.
size_t heap_bytes_in_use();

struct LeanRedisOptions {
  MiniRedisOptions base;
  EvictionConfig eviction;  // maxmemory 0 = unlimited
};

class LeanRedis {
 public:
  // Everything MiniRedis registers, plus:
  //   - the zset commands (db/zsets);
  //   - CONFIG GET/SET for maxmemory and maxmemory-policy (given, see lean_redis.cc);
  //   - a runner on every new client: denyoom commands first check memory, measured
  //     with heap_bytes_in_use() at most once per millisecond (reuse the last answer in
  //     between); if the Evictor can't get under maxmemory, reply kOomError.
  LeanRedis(const Clock& clock, LeanRedisOptions options = {});

  uint16_t listen(std::string_view host, uint16_t port) { return base_.listen(host, port); }
  void run() { base_.run(); }
  void stop() { base_.stop(); }

  MiniRedis& base() { return base_; }
  Evictor& evictor() { return evictor_; }

 private:
  void register_config_command();  // given

  const Clock& clock_;
  MiniRedis base_;
  Rng rng_;
  Evictor evictor_;
  int64_t last_memory_check_ms_ = -1;
  bool memory_ok_ = true;
};

}  // namespace rd
