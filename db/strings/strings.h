#pragma once

// db/strings: the string commands. The spec is this file; the story is README.md.

#include <string>
#include <unordered_map>

#include "cmd/dispatch/dispatch.h"

namespace rd {

// The whole database, for now: every value is a string. db/keyspace replaces the map
// with your own dict and adds the other types.
struct StringDb {
  std::unordered_map<std::string, std::string> map;
};

// Registers these commands on `table`, all working on `db` (arity in brackets):
//
//   GET key [2]                        value, or null
//   SET key value [NX|XX] [GET] [-3]   NX: only if missing; XX: only if present; both:
//                                      "ERR syntax error"; GET: reply the old value (or
//                                      null) instead of +OK. Options ignore case and may
//                                      repeat; anything else: "ERR syntax error". Without
//                                      GET, a SET that NX/XX skipped replies null.
//   DEL key [key ...] [-2]             number of keys removed (a repeated key counts once)
//   EXISTS key [key ...] [-2]          number of args that exist (repeats count again)
//   INCR key [2], DECR key [2]         +1 / -1, see INCRBY
//   INCRBY key n [3]                   value + n. A missing key counts as 0. The value
//                                      and n must pass resp::parse_int64, else "ERR value
//                                      is not an integer or out of range". If the sum
//                                      overflows: "ERR increment or decrement would
//                                      overflow", and the value is unchanged. Stores the
//                                      result as decimal text and replies it as :integer.
//   DECRBY key n [3]                   INCRBY key -n; n == INT64_MIN: "ERR decrement
//                                      would overflow"
//   APPEND key value [3]               appends (creating the key); replies the new length
//   STRLEN key [2]                     length, 0 if missing
//   MGET key [key ...] [-2]            array of values / nulls
//   MSET key value [key value ...] [-3]  sets all; an odd number of args after the name
//                                      gets arity_error("mset"); replies +OK
//
// Replies byte-for-byte as Redis 8.10 does (see the tests).
void register_string_commands(CommandTable& table, StringDb& db);

// Optional. Ranges inside a string:
//
//   GETRANGE key start end [4]   bytes start..end inclusive. Both may be negative
//                                (-1 = last byte). If both are negative and start > end:
//                                "". Then add the length to negatives, clamp them at 0,
//                                clamp end at length-1; start > end or an empty string
//                                gives "". A missing key is "". Non-integer: "ERR value
//                                is not an integer or out of range".
//   SETRANGE key offset value [4]  overwrite at offset, padding with '\0' bytes; replies
//                                the new length. offset < 0: "ERR offset is out of
//                                range". An empty value changes nothing and replies the
//                                current length (0 for a missing key, which is not
//                                created). offset + |value| > 512 MB: "ERR string
//                                exceeds maximum allowed size (proto-max-bulk-len)",
//                                checked before anything is allocated.
void register_string_range_commands(CommandTable& table, StringDb& db);

}  // namespace rd
