#include "msg/pubsub/pubsub.h"

#include "db/glob/glob.h"
#include "resp/encoder/encoder.h"

namespace rd {

void PubSub::subscribe(Session& s, std::span<const std::string> channels, std::string& reply) {
  // TODO(you)
}

void PubSub::psubscribe(Session& s, std::span<const std::string> patterns, std::string& reply) {
  // TODO(you)
}

void PubSub::unsubscribe(Session& s, std::span<const std::string> channels, std::string& reply) {
  // TODO(you)
}

void PubSub::punsubscribe(Session& s, std::span<const std::string> patterns, std::string& reply) {
  // TODO(you)
}

size_t PubSub::publish(std::string_view channel, std::string_view message) {
  // TODO(you): exact subscribers first, then every pattern that glob-matches.
  return 0;
}

size_t PubSub::subscriptions(const Session& s) const {
  // TODO(you)
  return 0;
}

void PubSub::forget(Session& s) {
  // TODO(you)
}

std::set<std::string> PubSub::channels(std::string_view pattern) const {
  // TODO(you)
  return {};
}

size_t PubSub::numsub(std::string_view channel) const {
  // TODO(you)
  return 0;
}

size_t PubSub::numpat() const {
  // TODO(you)
  return 0;
}

void register_pubsub_commands(CommandTable& table, PubSub& pubsub) {
  // TODO(you)
}

bool pubsub_gate(const PubSub& pubsub, Session& s, std::span<const std::string> args, std::string& reply) {
  // TODO(you)
  return false;
}

}  // namespace rd
