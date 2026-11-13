#pragma once

// server/messaging_redis: Mini Redis plus pub/sub, transactions and blocking pops. The
// story is README.md.

#include <cstdint>
#include <string_view>

#include "common/clock.h"
#include "msg/blocking/blocking.h"
#include "msg/pubsub/pubsub.h"
#include "msg/transactions/transactions.h"
#include "server/mini_redis/mini_redis.h"

namespace rd {

class MessagingRedis {
 public:
  // Everything MiniRedis has, plus the pub/sub and blocking commands, wired so that:
  //   - every request of a client goes: pubsub_gate, else transactions.handle, else
  //     table.dispatch; and after it, blocking.serve_ready();
  //   - BLPOP may block unless the transaction module is executing an EXEC;
  //   - blocking a client calls Client::block(); unblocking calls Client::unblock() and
  //     then Client::process_input() so the requests it sent meanwhile run;
  //   - a disconnecting client is forgotten by all three modules (the close hook).
  explicit MessagingRedis(const Clock& clock, MiniRedisOptions options = {});

  uint16_t listen(std::string_view host, uint16_t port) { return base_.listen(host, port); }
  void run() { base_.run(); }
  void stop() { base_.stop(); }
  MiniRedis& base() { return base_; }

 private:
  const Clock& clock_;
  MiniRedis base_;
  PubSub pubsub_;
  Transactions tx_;
  Blocking blocking_;
};

}  // namespace rd
