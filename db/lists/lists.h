#pragma once

// db/lists: the list type. The spec is this file; the story is README.md.

#include <deque>
#include <string>

#include "cmd/dispatch/dispatch.h"
#include "db/keyspace/keyspace.h"

namespace rd {

class ListValue final : public Value {
 public:
  static constexpr Type kType = Type::kList;
  Type type() const override { return kType; }
  // Real Redis reports "listpack" for small lists; this course always says "quicklist".
  std::string_view encoding() const override { return "quicklist"; }
  bool empty() const override { return items.empty(); }
  size_t approx_bytes() const override {
    size_t n = sizeof(*this);
    for (const auto& s : items) n += sizeof(s) + s.capacity();
    return n;
  }

  std::deque<std::string> items;
};

// Registers (arity in brackets). Indexes count from 0 at the head; negative ones from
// the tail (-1 is the last element). A list whose last element is removed is deleted.
// Non-integer indexes: "ERR value is not an integer or out of range". A key of another
// type: kWrongTypeError.
//   LPUSH key e [e ...] [-3]   push each in turn at the head (LPUSH l a b -> b a); the
//                              new length. RPUSH [-3]: at the tail.
//   LPUSHX, RPUSHX [-3]        the same, only if the list exists; else :0.
//   LPOP key [count] [-2]      without count: the head element, or null. With count: an
//                              array of up to count elements, a null array (*-1) if the
//                              key is missing, *0 for count 0. A count that isn't a
//                              non-negative integer: "ERR value is out of range, must be
//                              positive". More than 3 args: arity_error("lpop"). RPOP:
//                              from the tail.
//   LLEN key [2]               length; 0 if missing.
//   LRANGE key start stop [4]  elements start..stop inclusive: negative indexes add the
//                              length; start < 0 becomes 0; stop >= length becomes
//                              length-1; start > stop or start >= length gives *0.
//   LINDEX key index [3]       the element, or null if out of range or missing.
//   LSET key index value [4]   +OK; "ERR no such key" if missing; "ERR index out of
//                              range" if out of range.
//   LTRIM key start stop [4]   keeps only LRANGE's range (maybe deleting the key); +OK,
//                              also for a missing key.
void register_list_commands(CommandTable& table, Keyspace& ks);

// Optional.
//   LREM key count element [4]   removes up to count occurrences from the head (count >
//                                0), from the tail (count < 0), or all (0); how many.
//   LINSERT key BEFORE|AFTER pivot element [5]
//                                the new length; -1 if pivot isn't found; 0 if the key is
//                                missing; other than BEFORE/AFTER: "ERR syntax error".
void register_list_optional_commands(CommandTable& table, Keyspace& ks);

}  // namespace rd
