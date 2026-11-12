#include "resp/parser/parser.h"

#include "common/check.h"

namespace rd::resp {

bool parse_int64(std::string_view s, int64_t* out) {
  // TODO(you): reject everything but '-'? [1-9][0-9]* and "0". Accumulate the magnitude
  // in uint64_t and check against 2^63 - 1 (or 2^63 when negative) before each step.
  return false;
}

ParseResult parse_request(std::string_view in) {
  // TODO(you): walk a position through `in`: the "*<n>" line, then n times a "$<len>"
  // line and a payload. Any missing byte means kIncomplete; check each line as soon as
  // it's complete.
  return {};
}

ParseResult parse_inline(std::string_view in) {
  RD_CHECK(!in.empty() && in[0] != '*');
  // TODO(you, optional): find the line, then split it with a small state machine
  // (outside quotes, in "double", in 'single').
  return {};
}

}  // namespace rd::resp
