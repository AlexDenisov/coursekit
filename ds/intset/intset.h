#pragma once

// ds/intset: Redis's intset.c, a set of integers as one sorted array. The spec is this
// file; the story is README.md.

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace rd {

// The bytes, exactly as Redis lays them out in memory and in RDB files:
//   uint32 encoding   bytes per element: 2, 4 or 8
//   uint32 length     number of elements
//   length elements   signed, `encoding` bytes each, sorted ascending, no duplicates
// All integers little-endian. The encoding is the smallest that fits *every* element
// ever added: adding one value that needs more bytes rewrites the whole array
// ("upgrade"); removing never downgrades.
class IntSet {
 public:
  IntSet();  // empty, encoding 2 (8 bytes)

  size_t size() const;
  int encoding() const;
  bool contains(int64_t v) const;  // O(log n): binary search
  bool add(int64_t v);             // false if present. O(n) (memmove)
  bool remove(int64_t v);          // false if absent
  int64_t at(size_t i) const;      // i-th smallest; i < size() (checked)

  std::string_view bytes() const { return blob_; }

  // Validates and adopts a blob (as found in an RDB file): encoding 2, 4 or 8; the size
  // matches the length; elements strictly ascending. nullopt if anything is off.
  static std::optional<IntSet> from_bytes(std::string_view b);

 private:
  std::string blob_;
};

}  // namespace rd
