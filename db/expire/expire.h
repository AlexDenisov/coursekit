#pragma once

// db/expire: TTL commands and active expiry. The spec is this file; the story is
// README.md.

#include <cstddef>

#include "cmd/dispatch/dispatch.h"
#include "common/rng.h"
#include "db/keyspace/keyspace.h"

namespace rd {

// Registers (arity in brackets; all on any type of value):
//   EXPIRE key seconds [NX|XX|GT|LT] [-3], PEXPIRE (ms), EXPIREAT (unix s), PEXPIREAT
//   (unix ms): set the TTL; :1 if set, :0 if the key is missing or the condition fails.
//     NX: only without a TTL. XX: only with one. GT: only if the new time is later (no TTL
//     counts as infinite, so GT never applies to it). LT: only if earlier (always, without
//     a TTL). Options ignore case.
//     Errors: a non-integer time: "ERR value is not an integer or out of range"; a time
//     whose absolute ms don't fit in int64: "ERR invalid expire time in '<cmd>' command"
//     (<cmd> lowercase); an unknown option: "ERR Unsupported option <opt>" (as sent); NX
//     with XX, GT or LT: "ERR NX and XX, GT or LT options at the same time are not
//     compatible"; GT with LT: "ERR GT and LT options at the same time are not
//     compatible".
//     A time at or before now deletes the key (still :1).
//   TTL key [2]: seconds left, rounded to nearest ((ms + 500) / 1000); -1 without a TTL;
//     -2 if missing. PTTL [2]: ms left.
//   EXPIRETIME key [2]: the absolute unix time in seconds (ms / 1000); -1 / -2 as TTL.
//     PEXPIRETIME [2]: in ms.
//   PERSIST key [2]: :1 if a TTL was removed, else :0.
void register_expire_commands(CommandTable& table, Keyspace& ks);

// Redis's numbers for the active cycle.
inline constexpr size_t kExpireSamplesPerLoop = 20;
inline constexpr size_t kExpireAcceptableStalePercent = 10;

struct ExpireCycleStats {
  size_t loops = 0;    // sampling rounds done
  size_t sampled = 0;  // keys looked at
  size_t expired = 0;  // keys deleted
};

// One run of active expiry (Redis calls it from serverCron, 10 times a second):
//   repeat at most max_loops times:
//     sample min(kExpireSamplesPerLoop, keys with a TTL) random keys from ks.expires()
//     (with rng; repeats allowed) and delete the ones whose TTL has passed (as a lookup
//     would: lazily, not counting as writes);
//     stop if at most kExpireAcceptableStalePercent % of this round's sample had expired,
//     or no key has a TTL any more.
// Never deletes a key whose TTL hasn't passed.
ExpireCycleStats active_expire_cycle(Keyspace& ks, Rng& rng, size_t max_loops = 16);

}  // namespace rd
