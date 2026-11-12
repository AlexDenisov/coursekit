#pragma once

// A stand-in list type for tests that need "a key holding something other than a
// string" before db/lists exists. Registers a fake RPUSH that creates one and replies
// :1, as the real one does on a new key.

#include <memory>
#include <string>

#include "db/keyspace/keyspace.h"
#include "resp/encoder/encoder.h"

namespace rd::testing {

class FakeList final : public Value {
 public:
  static constexpr Type kType = Type::kList;
  Type type() const override { return kType; }
  std::string_view encoding() const override { return "listpack"; }
  size_t approx_bytes() const override { return sizeof(*this); }
};

inline void register_fake_rpush(CommandTable& table, Keyspace& ks) {
  table.add({"rpush", -3, [&ks](CommandContext& c) {
               ks.set(c.args[1], std::make_unique<FakeList>());
               resp::append_integer(c.reply, 1);
             }});
}

}  // namespace rd::testing
