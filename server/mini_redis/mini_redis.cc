#include "server/mini_redis/mini_redis.h"

#include "db/expire/expire.h"
#include "db/generic/generic.h"
#include "db/hashes/hashes.h"
#include "db/lists/lists.h"
#include "db/sets/sets.h"

namespace rd {

MiniRedis::MiniRedis(const Clock& clock, MiniRedisOptions options)
    : clock_(clock), options_(options), rng_(options.seed), ks_(clock), server_(table_, clock, options.limits) {
  // TODO(you): register every command family on table_ (they all share ks_), then add a
  // timer to server_.loop().timers() that calls cron() every 1000 / hz ms.
}

void MiniRedis::cron() {
  // TODO(you): active expiry, then incremental rehashing.
}

}  // namespace rd
