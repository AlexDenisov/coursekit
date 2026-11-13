#pragma once

// msg/blocking: BLPOP and BRPOP. The spec is this file; the story is README.md.

#include <cstddef>
#include <deque>
#include <functional>
#include <map>
#include <span>
#include <string>
#include <vector>

#include "cmd/dispatch/dispatch.h"
#include "common/clock.h"
#include "db/keyspace/keyspace.h"
#include "net/timers/timers.h"

namespace rd {

class Blocking {
 public:
  // on_block(s): the server must stop running s's further requests. on_unblock(s): it may
  // resume them (s's reply has already been pushed to it).
  Blocking(Keyspace& ks, Timers& timers, const Clock& clock, std::function<void(Session&)> on_block,
           std::function<void(Session&)> on_unblock)
      : ks_(ks), timers_(timers), clock_(clock), on_block_(std::move(on_block)), on_unblock_(std::move(on_unblock)) {}

  // BLPOP (left = true) / BRPOP: args = {name, key..., timeout}. Returns true if it replied
  // now (into reply), false if s is now blocked (nothing written; the reply will be pushed).
  //   timeout: seconds as a float, 0 = forever. Not a number: "ERR timeout is not a float
  //     or out of range"; negative: "ERR timeout is negative".
  //   A key holding another type: kWrongTypeError.
  //   The first key, in argument order, holding a list: pop from its head (BLPOP) or
  //     tail (BRPOP), reply [key, element] (deleting the key if that emptied it).
  //   Nothing to pop and !may_block (inside EXEC, no session): reply *-1.
  //   Else: s waits on all the keys (behind anyone already waiting on them), on_block(s);
  //     with a timeout, a timer pushes *-1 to s and unblocks it when it expires.
  bool pop(Session& s, std::span<const std::string> args, bool left, bool may_block, std::string& reply);

  // Call after every command. For each key that has waiters and now holds elements, serve
  // its waiters, longest-waiting first, while elements remain: pop for them (their own
  // direction), push [key, element], forget their other keys and timer, on_unblock.
  void serve_ready();

  bool blocked(const Session& s) const;
  size_t blocked_count() const { return waiting_.size(); }
  // Drops s's wait (it disconnected) without replying.
  void forget(Session& s);

 private:
  struct Wait {
    std::vector<std::string> keys;
    bool left = true;
    TimerId timer = 0;
  };
  void unblock(Session& s);  // forget the wait, cancel its timer, on_unblock

  Keyspace& ks_;
  Timers& timers_;
  const Clock& clock_;
  std::function<void(Session&)> on_block_, on_unblock_;
  std::map<Session*, Wait> waiting_;
  std::map<std::string, std::deque<Session*>, std::less<>> by_key_;  // waiters, oldest first
};

// BLPOP key [key ...] timeout [-3], BRPOP [-3]. A request may block only if it has a
// session and may_block(session) says so (the server says no inside EXEC).
void register_blocking_commands(CommandTable& table, Blocking& blocking, std::function<bool(Session*)> may_block);

}  // namespace rd
