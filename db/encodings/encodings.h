#pragma once

// db/encodings: small hashes, sets and sorted sets in compact form, converting to the
// big structures past Redis's thresholds. The spec is this file; the story is README.md.

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "ds/dict/dict.h"
#include "ds/intset/intset.h"
#include "ds/listpack/listpack.h"
#include "ds/skiplist/skiplist.h"

namespace rd {

// Redis 8's defaults (CONFIG GET *-max-*).
struct EncodingLimits {
  size_t hash_max_listpack_entries = 512;
  size_t hash_max_listpack_value = 64;  // bytes, for fields and values alike
  size_t set_max_intset_entries = 512;
  size_t set_max_listpack_entries = 128;
  size_t set_max_listpack_value = 64;
  size_t zset_max_listpack_entries = 128;
  size_t zset_max_listpack_value = 64;
};

// A hash: a listpack of field, value, field, value... while it has at most
// hash_max_listpack_entries fields and every field and value has at most
// hash_max_listpack_value bytes; then, for good, a Dict. Updating a field keeps its
// place; new fields go at the end.
class CompactHash {
 public:
  explicit CompactHash(EncodingLimits limits = {}) : limits_(limits) {}
  bool set(std::string_view field, std::string_view value);  // true if the field is new
  std::optional<std::string> get(std::string_view field) const;
  bool remove(std::string_view field);
  size_t size() const;
  std::string_view encoding() const;  // "listpack" or "hashtable"
  // All pairs; in insertion order while a listpack.
  std::vector<std::pair<std::string, std::string>> items() const;

 private:
  EncodingLimits limits_;
  Listpack lp_;
  bool converted_ = false;
  Dict<std::string> dict_;
};

// A set: an intset while every member passes parse_int64 and there are at most
// set_max_intset_entries; else a listpack (insertion order) while at most
// set_max_listpack_entries members of at most set_max_listpack_value bytes; else a Dict.
// Conversions (never back):
//   intset + integer, would exceed set_max_intset_entries      -> hashtable
//   intset + non-integer: if the new size <= set_max_listpack_entries and the member
//     fits set_max_listpack_value -> listpack (old members in ascending order, then
//     the new one); else -> hashtable
//   listpack + member, would exceed either listpack limit      -> hashtable
//   an empty set takes the encoding of its first member: intset for an integer, else
//     listpack if it fits, else hashtable.
class CompactSet {
 public:
  explicit CompactSet(EncodingLimits limits = {}) : limits_(limits) {}
  bool add(std::string_view member);  // true if new
  bool remove(std::string_view member);
  bool contains(std::string_view member) const;
  size_t size() const;
  std::string_view encoding() const;  // "intset", "listpack" or "hashtable"
  // Ascending for an intset, insertion order for a listpack, any order otherwise.
  std::vector<std::string> members() const;

 private:
  enum class Enc { kIntset, kListpack, kHashtable };
  EncodingLimits limits_;
  Enc enc_ = Enc::kIntset;
  IntSet intset_;
  Listpack lp_;
  Dict<bool> dict_;
};

// Optional. A sorted set: a listpack of member, score, member, score... kept sorted by
// (score, member), while at most zset_max_listpack_entries members of at most
// zset_max_listpack_value bytes; then a skiplist + dict. Scores are stored as text in the
// listpack, with enough digits to read back exactly (17 significant digits always do).
class CompactZSet {
 public:
  explicit CompactZSet(EncodingLimits limits = {}) : limits_(limits) {}
  bool add(std::string_view member, double score);  // true if new; else updates the score
  bool remove(std::string_view member);
  std::optional<double> score(std::string_view member) const;
  size_t size() const;
  std::string_view encoding() const;  // "listpack" or "skiplist"
  // (member, score) in ascending (score, member) order.
  std::vector<std::pair<std::string, double>> items() const;

 private:
  EncodingLimits limits_;
  Listpack lp_;
  bool converted_ = false;
  SkipList order_;
  Dict<double> scores_;
};

}  // namespace rd
