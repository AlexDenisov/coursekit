#include "server/messaging_redis/messaging_redis.h"

#include "net/client/client.h"

namespace rd {

MessagingRedis::MessagingRedis(const Clock& clock, MiniRedisOptions options)
    : clock_(clock),
      base_(clock, options),
      tx_(base_.keyspace()),
      blocking_(
          base_.keyspace(), base_.server().loop().timers(), clock,
          [](Session& s) {
            // TODO(you): block the client (every Session here is a Client).
          },
          [](Session& s) {
            // TODO(you): unblock it and let it run what it sent meanwhile.
          }) {
  // TODO(you): register the pub/sub and blocking commands; set the new-client hook
  // (the runner) and the close hook.
}

}  // namespace rd
