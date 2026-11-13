#pragma once

// persist/bgsave: snapshots without stopping the server, with fork(). The spec is this
// file; the story is README.md.

#include <sys/types.h>

#include <cstdint>
#include <optional>
#include <string>

#include "cmd/dispatch/dispatch.h"
#include "db/keyspace/keyspace.h"
#include "net/event_loop/event_loop.h"

namespace rd {

class BackgroundSaver {
 public:
  BackgroundSaver(Keyspace& ks, EventLoop& loop, std::string path)
      : ks_(ks), loop_(loop), path_(std::move(path)) {}

  // BGSAVE: fork(). The child calls rdb::save_snapshot(ks, path) and _exit()s with 0 on
  // success, 1 on failure (never returns, never runs destructors or touches the parent's
  // sockets). The parent returns at once and schedules a timer on the loop (every 10 ms)
  // that calls poll() until the child is reaped. False if a save is already running or
  // fork() fails.
  bool start();

  bool in_progress() const { return child_ > 0; }

  // Reaps the child if it has exited (waitpid with WNOHANG). On exit: records success
  // and, if it succeeded, last_save_ms = now. Returns true if the child was reaped by
  // this call.
  bool poll();

  // SAVE: write the snapshot in this process, blocking. False if a BGSAVE is running or
  // the write fails. Sets last_save_ms on success.
  bool save_now();

  // Status of the last save that finished (nullopt if none yet).
  std::optional<bool> last_status() const { return last_status_; }
  int64_t last_save_ms() const { return last_save_ms_; }
  const std::string& path() const { return path_; }

 private:
  Keyspace& ks_;
  EventLoop& loop_;
  std::string path_;
  pid_t child_ = -1;
  std::optional<bool> last_status_;
  int64_t last_save_ms_ = 0;
};

// SAVE [1]:     +OK, or "ERR Background save already in progress", or "ERR" on failure.
// BGSAVE [1]:   "+Background saving started", or "ERR Background save already in
//               progress".
// LASTSAVE [1]: :<unix seconds of the last successful save> (0 if none).
void register_save_commands(CommandTable& table, BackgroundSaver& saver);

}  // namespace rd
