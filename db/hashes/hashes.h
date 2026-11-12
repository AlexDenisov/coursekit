#pragma once

// db/hashes: the hash type, a dict inside a dict. The spec is this file; the story is
// README.md.

#include <string>

#include "cmd/dispatch/dispatch.h"
#include "db/keyspace/keyspace.h"
#include "ds/dict/dict.h"

namespace rd {

class HashValue final : public Value {
 public:
  static constexpr Type kType = Type::kHash;
  Type type() const override { return kType; }
  // Small hashes are "listpack" in real Redis; see db/encodings.
  std::string_view encoding() const override { return "hashtable"; }
  bool empty() const override { return fields.empty(); }
  size_t approx_bytes() const override;

  Dict<std::string> fields;  // field -> value
};

// Registers (arity in brackets). A hash whose last field is deleted is deleted. A key of
// another type: kWrongTypeError. Field order in replies is unspecified (it's your dict's).
//   HSET key f v [f v ...] [-4]   sets the fields; replies how many were new. An odd
//                                 number of field/value args: arity_error("hset").
//   HSETNX key f v [4]            :1 if f was new and is now set, else :0.
//   HGET key f [3]                the value, or null.
//   HMGET key f [f ...] [-3]      array of values / nulls.
//   HDEL key f [f ...] [-3]       how many were removed.
//   HLEN key [2], HEXISTS key f [3], HSTRLEN key f [3]   (0 for missing).
//   HGETALL key [2]               field, value, field, value, ... (*0 if missing).
//   HKEYS key [2], HVALS key [2]
//   HINCRBY key f n [4]           like INCRBY on one field (a missing one is 0): n not an
//                                 integer: "ERR value is not an integer or out of range";
//                                 the field's value not an integer: "ERR hash value is not
//                                 an integer"; overflow: "ERR increment or decrement would
//                                 overflow".
void register_hash_commands(CommandTable& table, Keyspace& ks);

}  // namespace rd
