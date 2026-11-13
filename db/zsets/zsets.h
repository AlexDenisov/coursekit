#pragma once

// db/zsets: sorted sets, a skiplist and a dict. The spec is this file; the story is
// README.md.

#include <string>
#include <string_view>

#include "cmd/dispatch/dispatch.h"
#include "db/keyspace/keyspace.h"
#include "ds/dict/dict.h"
#include "ds/skiplist/skiplist.h"

namespace rd {

class ZSetValue final : public Value {
 public:
  static constexpr Type kType = Type::kZSet;
  Type type() const override { return kType; }
  // Small zsets are "listpack" in real Redis; see db/encodings.
  std::string_view encoding() const override { return "skiplist"; }
  bool empty() const override { return scores.empty(); }
  size_t approx_bytes() const override;

  // Adds member or changes its score; true if it was new.
  bool set(std::string_view member, double score);
  // Removes member; false if absent.
  bool remove(std::string_view member);

  SkipList order;          // (score, member) in order
  Dict<double> scores;     // member -> score; both always hold the same elements
};

// A score as Redis prints it (util.c's d2string):
//   "inf", "-inf"; "0" for both zeros;
//   an integral value with |v| <= 2^62: as an integer ("100", "10000000000000000");
//   anything else: the shortest digits that parse back to exactly v (std::to_chars
//   gives them), laid out like Redis's fpconv_dtoa: with digits d (n of them) and v =
//   d * 10^K,
//     K >= 0 and |K + n - 1| < n + 7:     digits then K zeros  ("12345678901234567000")
//     K < 0 and (K > -7 or |K + n - 1| < 4): a plain decimal  ("1.5", "0.00001")
//     otherwise: scientific, "d.ddde+XX" with at least two exponent digits ("1e+20",
//     "5e-324", "1.7976931348623157e+308").
std::string format_score(double v);

// Parses a score like Redis (strtod): "1.5", "-2", "1e3", "+inf", "-inf", even "0x10";
// but not empty strings, leading spaces, trailing garbage or NaN. False if invalid.
bool parse_score(std::string_view s, double* out);

// Registers (arity in brackets). Scores are replied with format_score. Bad scores: "ERR
// value is not a valid float". A key of another type: kWrongTypeError. A zset whose last
// member is removed is deleted.
//   ZADD key [NX|XX] [GT|LT] [CH] [INCR] score member [score member ...] [-4]
//       Options first, any order and case. Errors, in this order: an odd number of
//       score/member args: "ERR syntax error"; NX with XX: "ERR XX and NX options at the
//       same time are not compatible"; GT, LT and NX together in any combination: "ERR
//       GT, LT, and/or NX options at the same time are not compatible"; INCR with more
//       than one pair: "ERR INCR option supports a single increment-element pair". All
//       scores are parsed before anything changes.
//       NX: only add new members. XX: only update existing ones. GT/LT: update an
//       existing member only if the new score is greater/less (new members are still
//       added). Reply: members added (+ members whose score changed, with CH). With
//       INCR: add the score to the member's (0 if new) and reply the new score, or null
//       if NX/XX/GT/LT prevented it; a NaN result: "ERR resulting score is not a number
//       (NaN)".
//   ZINCRBY key increment member [4]   ZADD INCR without options.
//   ZSCORE key member [3]              the score or null.
//   ZMSCORE key member [member ...] [-3]
//   ZCARD key [2]
//   ZRANK key member [WITHSCORE] [-3], ZREVRANK: 0-based rank (from the top for REV), or
//       null; WITHSCORE: [rank, score].
//   ZREM key member [member ...] [-3]   how many were removed.
//   ZCOUNT key min max [4]              members with min <= score <= max.
//   ZRANGE key start stop [BYSCORE] [REV] [LIMIT offset count] [WITHSCORES] [-4]
//       By index (LRANGE's rules; with REV counted from the highest score), or BYSCORE:
//       start/stop are scores (with REV: max then min). Score bounds: a number, "(x" for
//       exclusive, "-inf"/"+inf"; else "ERR min or max is not a float". LIMIT (BYSCORE
//       only, else "ERR syntax error, LIMIT is only supported in combination with either
//       BYSCORE or BYLEX"; missing numbers: "ERR syntax error"): skip offset (negative:
//       nothing), return at most count (negative: all). WITHSCORES: member, score,
//       member, score...
//   ZRANGEBYSCORE key min max [WITHSCORES] [LIMIT offset count] [-4]   the old form of
//       ZRANGE ... BYSCORE.
void register_zset_commands(CommandTable& table, Keyspace& ks);

}  // namespace rd
