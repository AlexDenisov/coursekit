#include "server/mini_redis/mini_redis.h"

#include "db/expire/expire.h"
#include "db/generic/generic.h"
#include "db/hashes/hashes.h"
#include "db/lists/lists.h"
#include "db/sets/sets.h"

namespace rd {

MiniRedis::MiniRedis(const Clock& clock, MiniRedisOptions options)
    : clock_(clock), options_(options), rng_(options.seed), ks_(clock), server_(table_, clock, options.limits) {
  register_connection_commands(table_);
  register_keyspace_commands(table_, ks_);
  register_expire_commands(table_, ks_);
  register_list_commands(table_, ks_);
  register_list_optional_commands(table_, ks_);
  register_hash_commands(table_, ks_);
  register_set_commands(table_, ks_);
  register_set_optional_commands(table_, ks_, rng_);
  register_generic_commands(table_, ks_, rng_);
  int64_t period = 1000 / std::max(1, options_.hz);
  server_.loop().timers().add(clock_.now_ms() + period, [this, period](TimerId) {
    cron();
    return period;
  });
}

void MiniRedis::cron() {
  active_expire_cycle(ks_, rng_);
  if (ks_.entries().is_rehashing()) ks_.entries().rehash_step(100);
  if (ks_.expires().is_rehashing()) ks_.expires().rehash_step(100);
}

}  // namespace rd
