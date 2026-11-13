#include "persist/aof_rewrite/aof_rewrite.h"

#include <sys/wait.h>
#include <unistd.h>

#include <cstdio>
#include <vector>

#include "db/hashes/hashes.h"
#include "db/lists/lists.h"
#include "db/sets/sets.h"
#include "db/zsets/zsets.h"
#include "resp/encoder/encoder.h"

namespace rd {

std::string rewrite_commands(const Keyspace& ks) {
  // TODO(you): for each live entry, emit commands as described (a helper that emits one
  // command per batch of 64 is handy), then PEXPIREAT.
  return "";
}

bool AofRewriter::start() {
  // TODO(you): fork; child: write the file with fwrite + fsync, _exit; parent: capture,
  // timer.
  return false;
}

bool AofRewriter::poll() {
  // TODO(you): waitpid WNOHANG; then the success or failure path.
  return false;
}

void register_rewrite_command(CommandTable& table, AofRewriter& rewriter) {
  // TODO(you)
}

}  // namespace rd
