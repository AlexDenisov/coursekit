#pragma once

// db/sets: the set type. The spec is this file; the story is README.md.

#include <string>

#include "cmd/dispatch/dispatch.h"
#include "common/rng.h"
#include "db/keyspace/keyspace.h"
#include "ds/dict/dict.h"

namespace rd {

class SetValue final : public Value {
 public:
  static constexpr Type kType = Type::kSet;
  Type type() const override { return kType; }
  // Small sets are "intset" or "listpack" in real Redis; see db/encodings.
  std::string_view encoding() const override { return "hashtable"; }
  bool empty() const override { return members.empty(); }
  size_t approx_bytes() const override;

  Dict<bool> members;  // the value is unused
};

// Registers (arity in brackets). A set whose last member is removed is deleted. A key of
// another type, anywhere in a multi-key command: kWrongTypeError. Member order in replies
// is unspecified.
//   SADD key m [m ...] [-3]          how many were new.
//   SREM key m [m ...] [-3]          how many were removed.
//   SCARD key [2], SISMEMBER key m [3]
//   SMISMEMBER key m [m ...] [-3]    an array of :1 / :0.
//   SMEMBERS key [2]
//   SINTER key [key ...] [-2]        a missing key is the empty set. Iterate the
//                                    smallest set and probe the others.
//   SUNION key [key ...] [-2], SDIFF key [key ...] [-2] (the first minus all others)
//   SINTERSTORE dst key [key ...] [-3], SUNIONSTORE, SDIFFSTORE: store the result at dst
//                                    (replacing it, deleting it if empty); its size.
//   SMOVE src dst m [4]              :1 if m was in src and is now in dst, else :0.
void register_set_commands(CommandTable& table, Keyspace& ks);

// Optional.
//   SPOP key [count] [-2]          without count: remove and reply a random member, or
//                                  null. With count: remove up to count distinct members,
//                                  an array (*0 for a missing key or count 0). count < 0
//                                  or not an integer: "ERR value is out of range, must be
//                                  positive".
//   SRANDMEMBER key [count] [-2]   the same without removing; count > 0: up to count
//                                  distinct members; count < 0: exactly -count members,
//                                  repeats allowed (*0 if missing). count not an integer:
//                                  "ERR value is not an integer or out of range".
void register_set_optional_commands(CommandTable& table, Keyspace& ks, Rng& rng);

}  // namespace rd
