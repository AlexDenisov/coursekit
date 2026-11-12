#include "db/expire/expire.h"

#include <limits>
#include <string>

#include "resp/encoder/encoder.h"
#include "resp/parser/parser.h"

namespace rd {

void register_expire_commands(CommandTable& table, Keyspace& ks) {
  // TODO(you): one helper for the four EXPIRE variants (unit: s or ms; relative or
  // absolute), one for TTL/PTTL/EXPIRETIME/PEXPIRETIME, and PERSIST.
}

ExpireCycleStats active_expire_cycle(Keyspace& ks, Rng& rng, size_t max_loops) {
  // TODO(you): copy each sampled key (random_key's pointer dies with the entry), check
  // ks.is_expired, and delete expired ones with ks.find (lazy expiry does the work).
  return {};
}

}  // namespace rd
