#include "db/eviction/eviction.h"

#include <algorithm>
#include <limits>

#include "resp/encoder/encoder.h"

namespace rd {

namespace {

bool eq(std::string_view a, std::string_view b) {
  if (a.size() != b.size()) return false;
  for (size_t i = 0; i < a.size(); ++i) {
    char x = a[i], y = b[i];
    if (x >= 'A' && x <= 'Z') x = static_cast<char>(x - 'A' + 'a');
    if (y >= 'A' && y <= 'Z') y = static_cast<char>(y - 'A' + 'a');
    if (x != y) return false;
  }
  return true;
}

}  // namespace

std::optional<EvictionPolicy> parse_policy(std::string_view name) {
  struct P {
    const char* name;
    EvictionPolicy p;
  };
  static const P kPolicies[] = {{"noeviction", EvictionPolicy::kNoEviction},
                                {"allkeys-lru", EvictionPolicy::kAllKeysLru},
                                {"volatile-lru", EvictionPolicy::kVolatileLru},
                                {"allkeys-random", EvictionPolicy::kAllKeysRandom},
                                {"volatile-random", EvictionPolicy::kVolatileRandom},
                                {"volatile-ttl", EvictionPolicy::kVolatileTtl}};
  for (const P& p : kPolicies) {
    if (eq(name, p.name)) return p.p;
  }
  return std::nullopt;
}

bool is_denyoom(std::string_view command) {
  static const char* const kDenyOom[] = {
      "set", "mset", "append", "incr", "decr", "incrby", "decrby", "setrange", "lpush", "rpush", "lpushx", "rpushx",
      "lset", "linsert", "hset", "hsetnx", "hincrby", "sadd", "smove", "sinterstore", "sunionstore", "sdiffstore",
      "zadd", "zincrby", "rename", "renamenx"};
  for (const char* c : kDenyOom) {
    if (eq(command, c)) return true;
  }
  return false;
}

uint32_t idle_seconds(uint32_t lru, uint32_t now_lru) {
  // TODO(you): handle the wrap of the 24-bit clock.
  return 0;
}

bool Evictor::make_room() {
  // TODO(you): the loop in eviction.h. Fill the pool with a helper; pick from it with
  // another.
  return false;
}

void dispatch_with_eviction(const CommandTable& table, Evictor& evictor, std::span<const std::string> args,
                            std::string& reply, Session* session) {
  // TODO(you): is_denyoom(args[0]) && !make_room() -> kOomError; else dispatch.
}

uint8_t lfu_log_incr(uint8_t counter, Rng& rng, int log_factor) {
  // TODO(you, optional)
  return counter;
}

uint8_t lfu_decay(uint8_t counter, uint64_t minutes_elapsed, uint64_t decay_minutes) {
  // TODO(you, optional)
  return counter;
}

}  // namespace rd
