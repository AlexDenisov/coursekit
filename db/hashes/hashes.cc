#include "db/hashes/hashes.h"

#include <limits>
#include <memory>

#include "resp/encoder/encoder.h"
#include "resp/parser/parser.h"

namespace rd {

size_t HashValue::approx_bytes() const {
  // TODO(you): sizeof(*this) plus, for each field, both strings' capacities and a few
  // dozen bytes of entry overhead.
  return sizeof(*this);
}

void register_hash_commands(CommandTable& table, Keyspace& ks) {
  // TODO(you): with lookup<HashValue>; create the HashValue on the first HSET; call
  // ks.erase_if_empty after HDEL.
}

}  // namespace rd
