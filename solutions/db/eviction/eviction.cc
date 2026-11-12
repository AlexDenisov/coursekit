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

bool volatile_policy(EvictionPolicy p) {
  return p == EvictionPolicy::kVolatileLru || p == EvictionPolicy::kVolatileRandom || p == EvictionPolicy::kVolatileTtl;
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
  return now_lru >= lru ? now_lru - lru : (0xFFFFFFu - lru) + now_lru + 1;
}

bool Evictor::make_room() {
  if (config_.maxmemory == 0) return true;
  bool vol = volatile_policy(config_.policy);
  while (used_memory_() > config_.maxmemory) {
    if (config_.policy == EvictionPolicy::kNoEviction) return false;
    std::string victim;
    if (config_.policy == EvictionPolicy::kAllKeysRandom || config_.policy == EvictionPolicy::kVolatileRandom) {
      const std::string* k = vol ? ks_.expires().random_key(rng_) : ks_.entries().random_key(rng_);
      if (k == nullptr) return false;
      victim = *k;
    } else {
      size_t pool_n = vol ? ks_.expires().size() : ks_.entries().size();
      if (pool_n == 0) return false;
      uint32_t now = lru_clock(ks_.clock().now_ms());
      for (size_t i = 0; i < config_.samples; ++i) {
        const std::string* k = vol ? ks_.expires().random_key(rng_) : ks_.entries().random_key(rng_);
        if (k == nullptr) break;
        uint64_t score;
        if (config_.policy == EvictionPolicy::kVolatileTtl) {
          score = std::numeric_limits<uint64_t>::max() - static_cast<uint64_t>(*ks_.expires().find(*k));
        } else {
          score = idle_seconds(ks_.entries().find(*k)->lru, now);
        }
        bool dup = false;
        for (auto& c : pool_) dup |= c.key == *k;
        if (dup) continue;
        if (pool_.size() == kEvictionPoolSize && score <= pool_.front().score) continue;
        auto pos = std::lower_bound(pool_.begin(), pool_.end(), score,
                                    [](const Candidate& c, uint64_t s) { return c.score < s; });
        pool_.insert(pos, {*k, score});
        if (pool_.size() > kEvictionPoolSize) pool_.erase(pool_.begin());
      }
      while (!pool_.empty()) {
        Candidate c = pool_.back();
        pool_.pop_back();
        if (ks_.entries().find(c.key) && (!vol || ks_.expires().find(c.key))) {
          victim = c.key;
          break;
        }
      }
      if (victim.empty()) continue;
    }
    ks_.erase(victim);
    ++evicted_;
  }
  return true;
}

void dispatch_with_eviction(const CommandTable& table, Evictor& evictor, std::span<const std::string> args,
                            std::string& reply, Session* session) {
  if (is_denyoom(args[0]) && !evictor.make_room()) {
    resp::append_error(reply, kOomError);
    return;
  }
  table.dispatch(args, reply, session);
}

uint8_t lfu_log_incr(uint8_t counter, Rng& rng, int log_factor) {
  if (counter == 255) return 255;
  double base = counter > 5 ? counter - 5 : 0;
  double p = 1.0 / (base * log_factor + 1);
  return rng.uniform() < p ? counter + 1 : counter;
}

uint8_t lfu_decay(uint8_t counter, uint64_t minutes_elapsed, uint64_t decay_minutes) {
  uint64_t steps = decay_minutes ? minutes_elapsed / decay_minutes : 0;
  return steps >= counter ? 0 : static_cast<uint8_t>(counter - steps);
}

}  // namespace rd
