#include "server/messaging_redis/messaging_redis.h"

#include "net/client/client.h"

namespace rd {

MessagingRedis::MessagingRedis(const Clock& clock, MiniRedisOptions options)
    : clock_(clock),
      base_(clock, options),
      tx_(base_.keyspace()),
      blocking_(
          base_.keyspace(), base_.server().loop().timers(), clock,
          [](Session& s) { static_cast<Client&>(s).block(); },
          [](Session& s) {
            auto& c = static_cast<Client&>(s);
            c.unblock();
            c.process_input();
          }) {
  register_pubsub_commands(base_.table(), pubsub_);
  register_blocking_commands(base_.table(), blocking_, [this](Session*) { return !tx_.executing(); });
  base_.server().set_new_client_hook([this](Client& client) {
    client.set_runner([this](Client& c, std::span<const std::string> args, std::string& reply) {
      if (!pubsub_gate(pubsub_, c, args, reply) && !tx_.handle(base_.table(), c, args, reply)) {
        base_.table().dispatch(args, reply, &c);
      }
      blocking_.serve_ready();
    });
  });
  base_.server().set_close_hook([this](Client& c) {
    pubsub_.forget(c);
    tx_.forget(c);
    blocking_.forget(c);
  });
}

}  // namespace rd
