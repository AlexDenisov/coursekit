#include "db/sets/sets.h"

#include <algorithm>
#include <memory>
#include <vector>

#include "resp/encoder/encoder.h"
#include "resp/parser/parser.h"

namespace rd {

size_t SetValue::approx_bytes() const {
  // TODO(you): sizeof(*this) plus, per member, its capacity and some entry overhead.
  return sizeof(*this);
}

void register_set_commands(CommandTable& table, Keyspace& ks) {
  // TODO(you): the commands. For the algebra, first collect the SetValue* for each key
  // (checking types; nullptr for missing), then compute; the STORE variants reuse that.
}

void register_set_optional_commands(CommandTable& table, Keyspace& ks, Rng& rng) {
  // TODO(you, optional): SPOP and SRANDMEMBER, with members.random_key(rng).
}

}  // namespace rd
