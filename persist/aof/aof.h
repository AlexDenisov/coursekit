#pragma once

// persist/aof: the append-only file. The spec is this file; the story is README.md.

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "cmd/dispatch/dispatch.h"
#include "common/clock.h"
#include "db/keyspace/keyspace.h"

namespace rd {

enum class FsyncPolicy { kAlways, kEverySec, kNo };

// The form a write is logged in, as Redis propagates it (tools/aof_reference.py): times
// relative to now become absolute, so replaying the log later expires keys at the same
// moment.
//   EXPIRE k s [opt], PEXPIRE k ms [opt], EXPIREAT k s [opt]  ->  PEXPIREAT k <unix ms> [opt]
//   SET k v ... EX s | PX ms | EXAT s ...                    ->  SET k v ... PXAT <unix ms> ...
// (Command and option names match case-insensitively; the rewritten ones are written
// uppercase.) Everything else is returned unchanged.
std::vector<std::string> log_form(std::span<const std::string> args, int64_t now_ms);

class Aof {
 public:
  Aof(std::string path, FsyncPolicy policy, const Clock& clock)
      : path_(std::move(path)), policy_(policy), clock_(clock) {}
  ~Aof();
  Aof(const Aof&) = delete;
  Aof& operator=(const Aof&) = delete;

  // Opens path for appending (creating it). False on error.
  bool open();
  // Closes and opens path again (after aof_rewrite renamed a new file over it).
  bool reopen();

  // Queues log_form(args) as a RESP request (an array of bulk strings) in memory; also
  // appends it to the capture buffer, if set.
  void append(std::span<const std::string> args);

  // Writes everything queued to the file (Redis does this before sending any replies, in
  // beforeSleep, so a client never sees +OK for a write that isn't in the file yet);
  // with kAlways, fsyncs too. False on a write error.
  bool flush();

  // Call periodically: with kEverySec, fsyncs if anything was written and the last fsync
  // (or open()) was >= 1000 ms ago.
  void cron();

  uint64_t fsync_count() const { return fsyncs_; }
  const std::string& path() const { return path_; }
  // While set, every appended request is also appended to *buf (persist/aof_rewrite).
  void set_capture(std::string* buf) { capture_ = buf; }

 private:
  std::string path_;
  FsyncPolicy policy_;
  const Clock& clock_;
  FILE* file_ = nullptr;
  std::string buffer_;
  std::string* capture_ = nullptr;
  bool unsynced_ = false;
  int64_t last_fsync_ms_ = 0;
  uint64_t fsyncs_ = 0;
};

// Runs one request and, if it changed the keyspace (ks.dirty() moved), appends it to the
// AOF. Reads and no-op writes (DEL of a missing key) are not logged.
void dispatch_logged(const CommandTable& table, Keyspace& ks, Aof& aof, std::span<const std::string> args,
                     std::string& reply, Session* session = nullptr);

struct ReplayResult {
  bool ok = false;
  size_t commands = 0;     // requests replayed
  bool truncated = false;  // the last request was incomplete and was ignored
  size_t valid_bytes = 0;  // bytes up to the end of the last complete request
  std::string error;
};

// Replays a log: parses every request (resp/parser) and runs it with table.dispatch,
// discarding replies. An incomplete last request (a crash in the middle of a write) is
// ignored: ok, truncated. A protocol error is an error.
ReplayResult replay_aof(std::string_view data, const CommandTable& table);

// replay_aof on a file. A missing file is fine (nothing to replay). If the tail was
// truncated, the file is cut back to valid_bytes so new appends follow a valid prefix.
ReplayResult load_aof_file(const std::string& path, const CommandTable& table);

}  // namespace rd
