#include "db/zsets/zsets.h"

#include <charconv>
#include <cmath>
#include <cstdlib>
#include <memory>

#include "resp/encoder/encoder.h"
#include "resp/parser/parser.h"

namespace rd {

size_t ZSetValue::approx_bytes() const {
  // TODO(you): per member: the string twice (skiplist node and dict entry), a node with
  // its levels, a dict entry.
  return sizeof(*this);
}

bool ZSetValue::set(std::string_view member, double score) {
  // TODO(you): keep `order` and `scores` in step.
  return false;
}

bool ZSetValue::remove(std::string_view member) {
  // TODO(you): both structures.
  return false;
}

std::string format_score(double v) {
  // TODO(you): the special cases, then std::to_chars(buf, end, v, std::chars_format::scientific)
  // gives the shortest digits as "d.ddde+XX": take the digits and the exponent and lay them
  // out as described.
  return "";
}

bool parse_score(std::string_view s, double* out) {
  // TODO(you): copy to a std::string (strtod wants a terminator), reject what strtod
  // would skip or leave behind.
  return false;
}

void register_zset_commands(CommandTable& table, Keyspace& ks) {
  // TODO(you): with lookup<ZSetValue>. Parse score ranges into (value, exclusive).
}

}  // namespace rd
