#pragma once

// db/generic: commands about keys rather than values. The spec is this file; the story
// is README.md.

#include "cmd/dispatch/dispatch.h"
#include "common/rng.h"
#include "db/keyspace/keyspace.h"

namespace rd {

// Registers (arity in brackets). Expired keys are never listed or counted as existing
// (DBSIZE excepted: it counts what's stored).
//   KEYS pattern [2]           every key matching the glob (db/glob), any order. The
//                              pattern "*" matches every key without calling the matcher
//                              (so it also matches the empty key, as in Redis).
//   SCAN cursor [MATCH p] [COUNT n] [TYPE t] [-2]
//                              cursor: an unsigned decimal, else "ERR invalid cursor".
//                              COUNT (default 10): an integer (else "ERR value is not an
//                              integer or out of range") >= 1 (else "ERR syntax error").
//                              An unknown option or a missing value: "ERR syntax error".
//                              Work: call dict_scan on the keyspace's entries until at
//                              least COUNT keys were visited or the cursor is back to 0,
//                              at most 10 * COUNT calls. Then drop expired keys and keys
//                              failing MATCH or TYPE (TYPE takes a type_name; an unknown
//                              one matches nothing). Reply: a 2-element array of the next
//                              cursor (as a bulk string) and an array of keys.
//   RENAME src dst [3]         moves the value and its TTL to dst, replacing dst; "ERR no
//                              such key" if src is missing; src == dst is fine (+OK).
//   RENAMENX src dst [3]       the same if dst doesn't exist: :1; else :0 and nothing
//                              happens.
//   DBSIZE [1]                 keys stored.
//   FLUSHDB [ASYNC|SYNC] [-1], FLUSHALL [-1]: delete everything; any other arg: "ERR
//                              syntax error".
//   RANDOMKEY [1]              a random key that hasn't expired (sampling with rng and
//                              deleting expired ones found on the way), or null.
void register_generic_commands(CommandTable& table, Keyspace& ks, Rng& rng);

}  // namespace rd
