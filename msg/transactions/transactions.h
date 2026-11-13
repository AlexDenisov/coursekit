#pragma once

// msg/transactions: MULTI/EXEC/WATCH. The spec is this file; the story is README.md.

#include <map>
#include <set>
#include <span>
#include <string>
#include <vector>

#include "cmd/dispatch/dispatch.h"
#include "db/keyspace/keyspace.h"

namespace rd {

class Transactions {
 public:
  // Installs the keyspace's on-modified and on-flush hooks: a write to a watched key (or
  // a flush) marks every session watching it as dirty.
  explicit Transactions(Keyspace& ks);

  // Runs before normal dispatch for each request of session s. Returns true if it handled
  // the request (and wrote the reply), false if the caller should dispatch it normally.
  //   MULTI     +OK, enter the transaction; nested: "ERR MULTI calls can not be nested"
  //   any other command while in MULTI (except EXEC, DISCARD, WATCH, UNWATCH, MULTI):
  //             table.check(args) == "": queue it, "+QUEUED"; else reply that error and
  //             mark the transaction aborted
  //   EXEC      not in MULTI: "ERR EXEC without MULTI". Aborted: "EXECABORT Transaction
  //             discarded because of previous errors." Dirty (a watched key changed since
  //             WATCH): the null array *-1, nothing runs. Else: run the queue in order
  //             with table.dispatch, and reply an array of their replies (errors inside
  //             it don't stop the others). In every case: leave MULTI, unwatch everything.
  //   DISCARD   not in MULTI: "ERR DISCARD without MULTI"; else drop the queue, unwatch
  //             everything, +OK
  //   WATCH k...  in MULTI: "ERR WATCH inside MULTI is not allowed" (the transaction goes
  //             on); else watch the keys, +OK
  //   UNWATCH   unwatch everything (also clears dirty), +OK
  // Outside MULTI, everything else returns false.
  bool handle(const CommandTable& table, Session& s, std::span<const std::string> args, std::string& reply);

  bool in_multi(const Session& s) const;
  // True while EXEC is running queued commands (msg/blocking: BLPOP must not block then).
  bool executing() const { return executing_; }
  // Drops everything about s (it disconnected).
  void forget(Session& s);

 private:
  struct State {
    bool multi = false;
    bool aborted = false;
    bool dirty = false;
    std::vector<std::vector<std::string>> queue;
    std::set<std::string> watched;
  };
  void unwatch_all(Session& s);

  Keyspace& ks_;
  std::map<const Session*, State> states_;
  std::map<std::string, std::set<Session*>, std::less<>> watchers_;
  bool executing_ = false;
};

}  // namespace rd
