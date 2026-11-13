#pragma once

// persist/aof_rewrite: compacting the AOF from a snapshot. The spec is this file; the
// story is README.md.

#include <sys/types.h>

#include <optional>
#include <string>

#include "cmd/dispatch/dispatch.h"
#include "db/keyspace/keyspace.h"
#include "net/event_loop/event_loop.h"
#include "persist/aof/aof.h"

namespace rd {

// Redis's AOF_REWRITE_ITEMS_PER_CMD: aggregates are rebuilt 64 elements per command.
inline constexpr size_t kRewriteItemsPerCommand = 64;

// The shortest log that rebuilds ks from nothing, as RESP requests. Per key (expired keys
// are left out):
//   string: SET k v
//   list:   RPUSH k e1 ... e64, RPUSH k e65 ... (head to tail)
//   set:    SADD k m1 ... (64 per command)
//   hash:   HSET k f1 v1 ... (64 pairs per command)
//   zset:   ZADD k s1 m1 ... (64 pairs per command; scores with format_score)
//   then, if the key has a TTL: PEXPIREAT k <unix ms>
std::string rewrite_commands(const Keyspace& ks);

class AofRewriter {
 public:
  AofRewriter(Keyspace& ks, Aof& aof, EventLoop& loop) : ks_(ks), aof_(aof), loop_(loop) {}

  // BGREWRITEAOF. fork(): the child writes rewrite_commands(ks) to aof.path() +
  // ".rewrite", fsyncs it and _exit()s (0 on success). The parent captures every request
  // appended to the AOF from now on (aof.set_capture): the child's snapshot can't contain
  // them. It polls the child from a timer, every 10 ms. False if a rewrite is already
  // running or fork() fails.
  bool start();
  bool in_progress() const { return child_ > 0; }

  // Reaps the child if it exited. Success: aof.flush(); append the captured requests to
  // the new file; rename it over aof.path(); aof.reopen(). Failure: discard the capture,
  // keep the old file. Capturing stops either way. True if the child was reaped now.
  bool poll();

  std::optional<bool> last_status() const { return last_status_; }

 private:
  Keyspace& ks_;
  Aof& aof_;
  EventLoop& loop_;
  pid_t child_ = -1;
  std::string captured_;
  std::optional<bool> last_status_;
};

// BGREWRITEAOF [1]: "+Background append only file rewriting started", or "ERR Background
// append only file rewriting already in progress".
void register_rewrite_command(CommandTable& table, AofRewriter& rewriter);

}  // namespace rd
