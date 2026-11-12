#pragma once

// db/glob: Redis's glob-style pattern matching (util.c's stringmatchlen), used by KEYS,
// SCAN MATCH (db/generic) and PSUBSCRIBE (msg/pubsub). The story is README.md.

#include <string_view>

namespace rd {

// True if `s` matches `pattern`, both byte strings:
//   *        any run of bytes, including none (consecutive stars act as one)
//   ?        exactly one byte
//   [abc]    one byte from the set;  [^abc]  one byte not in it
//   [a-z]    a range inside a set (a reversed range [z-a] means the same); a '-' that
//            isn't between two bytes is literal
//   \x       x literally, outside or inside a set; a trailing lone '\' is literal
//   other    itself
// Quirks kept from Redis (the tests come from it): a set that never closes ends at the
// end of the pattern ("a[b" matches "ab"); "[]" never matches anything because ']'
// right after '[' closes the set; the empty string matches only the empty pattern,
// not even "*" (KEYS * gets the empty key through a fast path that skips matching).
// Must run in O(|pattern| * |s|): no exponential backtracking.
bool glob_match(std::string_view pattern, std::string_view s);

}  // namespace rd
