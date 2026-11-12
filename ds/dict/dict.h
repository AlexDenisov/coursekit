#pragma once

// ds/dict: Redis's dict.c, a hash table that resizes a little at a time. The spec is
// this file; your code goes in dict_impl.h; the story is README.md.

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "common/rng.h"

namespace rd {

// The hash function: FNV-1a with a final mix, fixed seed. Given. (Redis uses SipHash
// with a random per-process key, so that clients can't choose keys that all collide.)
inline uint64_t dict_hash(std::string_view key) {
  uint64_t h = 0xcbf29ce484222325ull;
  for (unsigned char c : key) h = (h ^ c) * 0x100000001b3ull;
  h ^= h >> 33;
  h *= 0xff51afd7ed558ccdull;
  h ^= h >> 33;
  return h;
}

// String keys to values of type V (movable).
//
// Two tables of chained buckets, sizes powers of two (or 0 = not allocated). Normally
// only table 0 is in use. While *rehashing*, entries migrate bucket by bucket from
// table 0 to table 1; when table 0 is empty, table 1 becomes table 0.
//
// Policy (Redis's):
//   - The first insert allocates 4 buckets.
//   - Grow: before inserting a new key, if not rehashing and size() >= buckets in
//     table 0, start rehashing into a table of the smallest power of two >= size() + 1.
//   - Shrink: after erasing, if not rehashing, table 0 has more than 4 buckets and
//     size() * 8 <= its buckets, start rehashing into max(4, smallest power of two >=
//     size()) buckets.
//   - Every non-const find, insert, get_or_insert and erase first does rehash_step(1)
//     if rehashing.
//   - While rehashing, new keys go into table 1; lookups and erases check both.
//   - Clearing frees both tables (0 buckets).
template <class V>
class Dict {
 public:
  using HashFn = uint64_t (*)(std::string_view);

  explicit Dict(HashFn hash = dict_hash) : hash_(hash) {}
  Dict(Dict&&) noexcept = default;
  Dict& operator=(Dict&&) noexcept = default;

  size_t size() const { return used_[0] + used_[1]; }
  bool empty() const { return size() == 0; }

  // The value for key, or nullptr. O(1) average.
  V* find(std::string_view key);
  const V* find(std::string_view key) const;  // no rehash step

  // Adds key -> value if key is absent: {the stored value, true}. If key is present:
  // {the existing value, false}, and `value` is discarded.
  std::pair<V*, bool> insert(std::string key, V value);

  // The value for key, inserting a default-constructed V if absent.
  V& get_or_insert(std::string_view key);

  // Removes key. False if absent.
  bool erase(std::string_view key);

  void clear();

  // Calls fn(const std::string& key, V& value) for every entry, each exactly once. fn
  // must not insert or erase. No rehash steps happen during the iteration.
  template <class F>
  void for_each(F&& fn);
  template <class F>
  void for_each(F&& fn) const;

  // The key of a random entry, or nullptr if empty: pick random buckets (across both
  // tables) until one is non-empty, then a random entry of its chain. Keys in short
  // chains are slightly favoured, as in Redis. Used by RANDOMKEY, active expiry and
  // eviction sampling.
  const std::string* random_key(Rng& rng) const;

  // ---- incremental rehashing ----
  bool is_rehashing() const { return rehash_index_ >= 0; }

  // Moves the entries of up to n non-empty buckets from table 0 to table 1, visiting at
  // most 10 * n empty buckets on the way. Finishes the rehash (table 1 becomes table 0)
  // when table 0 is empty. Returns true if there's more to do. No-op when not rehashing.
  bool rehash_step(size_t n);

  // Buckets in table t (0 or 1); 0 if it isn't allocated.
  size_t bucket_count(int t) const { return tables_[t].size(); }

  // For ds/dict_scan: calls fn(const std::string& key, const V& value) for each entry in
  // bucket `index` of table t.
  template <class F>
  void for_each_in_bucket(int t, size_t index, F&& fn) const;

  // The bucket a key hashes to in table t (requires bucket_count(t) > 0).
  size_t bucket_of(std::string_view key, int t) const { return hash_(key) & (tables_[t].size() - 1); }

 private:
  struct Entry {
    std::string key;
    V value;
    std::unique_ptr<Entry> next;
  };
  using Table = std::vector<std::unique_ptr<Entry>>;

  Entry* find_entry(std::string_view key) const;
  void start_rehash(size_t buckets);
  void grow_if_needed();
  void shrink_if_needed();

  HashFn hash_;
  Table tables_[2];
  size_t used_[2] = {0, 0};
  int64_t rehash_index_ = -1;  // next bucket of table 0 to migrate; -1 = not rehashing
};

}  // namespace rd

#include "ds/dict/dict_impl.h"
