#pragma once

// db/eviction: maxmemory and approximate LRU (evict.c). The spec is this file; the story
// is README.md.

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "cmd/dispatch/dispatch.h"
#include "common/rng.h"
#include "db/keyspace/keyspace.h"

namespace rd {

enum class EvictionPolicy { kNoEviction, kAllKeysLru, kVolatileLru, kAllKeysRandom, kVolatileRandom, kVolatileTtl };

// "noeviction", "allkeys-lru", "volatile-lru", "allkeys-random", "volatile-random",
// "volatile-ttl" (case-insensitive), as CONFIG SET maxmemory-policy takes them. Given.
std::optional<EvictionPolicy> parse_policy(std::string_view name);

struct EvictionConfig {
  size_t maxmemory = 0;  // bytes; 0 = no limit
  EvictionPolicy policy = EvictionPolicy::kNoEviction;
  size_t samples = 5;  // maxmemory-samples
};

inline constexpr size_t kEvictionPoolSize = 16;
inline constexpr char kOomError[] = "OOM command not allowed when used memory > 'maxmemory'.";

// Seconds since `lru` on Redis's 24-bit clock (see lru_clock), which wraps every 2^24 s.
uint32_t idle_seconds(uint32_t lru, uint32_t now_lru);

class Evictor {
 public:
  // used_memory() measures the bytes in use (the server's heap, or a fake in tests).
  Evictor(Keyspace& ks, Rng& rng, EvictionConfig config, std::function<size_t()> used_memory)
      : ks_(ks), rng_(rng), config_(config), used_memory_(std::move(used_memory)) {}

  // Evicts keys until used_memory() <= maxmemory (Redis's performEvictions). Returns true
  // if memory is (now) under the limit or there is no limit; false if the policy is
  // noeviction or no eligible key is left (volatile-* with no TTLs, an empty keyspace).
  //   *-random: delete a random key (from all keys, or from those with a TTL).
  //   *-lru, volatile-ttl: sample `samples` random keys (all, or those with a TTL), score
  //   each (lru: idle seconds; ttl: the earlier the expiry, the higher), and merge them
  //   into the pool: at most kEvictionPoolSize candidates, best kept, no duplicates. Then
  //   delete the best candidate that still exists (and still has a TTL for volatile-*),
  //   removing it from the pool.
  // Each deletion goes through ks.erase (it's a write).
  bool make_room();

  size_t evicted() const { return evicted_; }
  const EvictionConfig& config() const { return config_; }
  void set_config(EvictionConfig c) { config_ = c; }

 private:
  struct Candidate {
    std::string key;
    uint64_t score;  // higher = evict first
  };
  Keyspace& ks_;
  Rng& rng_;
  EvictionConfig config_;
  std::function<size_t()> used_memory_;
  std::vector<Candidate> pool_;  // kept sorted by score
  size_t evicted_ = 0;
};

// Commands that can grow memory (Redis's "denyoom" flag): SET, MSET, APPEND, INCR..., LPUSH,
// RPUSH, LSET, LINSERT, HSET, HSETNX, HINCRBY, SADD, SMOVE, S*STORE, ZADD, ZINCRBY,
// RENAME... Given.
bool is_denyoom(std::string_view command);

// Runs one request, first calling make_room() if the command is denyoom; if that fails,
// replies kOomError instead of running it. Other commands (GET, DEL, ...) always run.
void dispatch_with_eviction(const CommandTable& table, Evictor& evictor, std::span<const std::string> args,
                            std::string& reply, Session* session = nullptr);

// Optional: LFU's logarithmic counter (Redis's LFULogIncr with lfu-log-factor). Counters
// start at 5; a hit increments with probability 1 / ((counter - 5) * log_factor + 1)
// (1 when counter <= 5); 255 is the maximum.
uint8_t lfu_log_incr(uint8_t counter, Rng& rng, int log_factor = 10);
// Optional: LFU decay: one step per `decay_minutes` elapsed, never below 0.
uint8_t lfu_decay(uint8_t counter, uint64_t minutes_elapsed, uint64_t decay_minutes = 1);

}  // namespace rd
