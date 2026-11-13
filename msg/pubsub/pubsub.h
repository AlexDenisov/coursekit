#pragma once

// msg/pubsub: publish/subscribe. The spec is this file; the story is README.md.

#include <cstddef>
#include <map>
#include <set>
#include <span>
#include <string>
#include <string_view>

#include "cmd/dispatch/dispatch.h"

namespace rd {

// Who listens to what. Sessions are identified by pointer; forget() them when they go.
class PubSub {
 public:
  // SUBSCRIBE: for each channel (in order), add it for s if new, and append the
  // confirmation ["subscribe", channel, n] to `reply`, where n is s's number of channels
  // plus patterns after this one. PSUBSCRIBE: the same with "psubscribe" and patterns.
  void subscribe(Session& s, std::span<const std::string> channels, std::string& reply);
  void psubscribe(Session& s, std::span<const std::string> patterns, std::string& reply);

  // UNSUBSCRIBE: for each channel given, or every channel s has if none are given:
  // remove it and append ["unsubscribe", channel, n]. If s has nothing to unsubscribe
  // from, append one ["unsubscribe", null, n]. PUNSUBSCRIBE: the same for patterns.
  void unsubscribe(Session& s, std::span<const std::string> channels, std::string& reply);
  void punsubscribe(Session& s, std::span<const std::string> patterns, std::string& reply);

  // Pushes ["message", channel, message] to every subscriber of channel, and
  // ["pmessage", pattern, channel, message] once per matching pattern subscription
  // (db/glob), with Session::push. A session subscribed both ways gets both. Returns the
  // number of messages pushed.
  size_t publish(std::string_view channel, std::string_view message);

  // Channels plus patterns of s (0: not in subscriber mode).
  size_t subscriptions(const Session& s) const;

  // Drops everything about s (it disconnected).
  void forget(Session& s);

  // PUBSUB CHANNELS [pattern]: channels with at least one subscriber (matching pattern).
  std::set<std::string> channels(std::string_view pattern = "*") const;
  size_t numsub(std::string_view channel) const;
  size_t numpat() const;  // pattern subscriptions in total

 private:
  std::map<std::string, std::set<Session*>, std::less<>> channels_;
  std::map<std::string, std::set<Session*>, std::less<>> patterns_;
  std::map<const Session*, std::pair<std::set<std::string>, std::set<std::string>>> by_session_;  // channels, patterns
};

// Registers (arity in brackets): SUBSCRIBE ch... [-2], UNSUBSCRIBE [ch...] [-1],
// PSUBSCRIBE p... [-2], PUNSUBSCRIBE [p...] [-1] (all need a session: without one,
// "ERR this command needs a connection"), PUBLISH ch msg [3] (:receivers), PUBSUB
// CHANNELS [pattern] | NUMSUB [ch...] | NUMPAT [-2].
void register_pubsub_commands(CommandTable& table, PubSub& pubsub);

// Subscriber mode: once s has a subscription, only SUBSCRIBE, PSUBSCRIBE, UNSUBSCRIBE,
// PUNSUBSCRIBE, PING, QUIT and RESET may run. Returns true if it handled the request (and
// wrote the reply): PING replies ["pong", message or ""]; other forbidden commands get
// "ERR Can't execute '<cmd>': only (P|S)SUBSCRIBE / (P|S)UNSUBSCRIBE / PING / QUIT / RESET
// are allowed in this context" (<cmd> lowercase). False: run the request normally.
bool pubsub_gate(const PubSub& pubsub, Session& s, std::span<const std::string> args, std::string& reply);

}  // namespace rd
