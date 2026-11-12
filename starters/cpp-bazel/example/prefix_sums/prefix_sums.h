#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace course {

// Sums of ranges of a fixed array in O(1) each, after O(n) preprocessing.
class PrefixSums {
 public:
  // O(n).
  explicit PrefixSums(std::span<const int32_t> a);

  // Number of elements of the original array.
  size_t size() const { return p_.empty() ? 0 : p_.size() - 1; }

  // a[l] + ... + a[r-1]; 0 when l == r. Requires l <= r <= size(). O(1).
  int64_t sum(size_t l, size_t r) const;

  // Optional. For an array of non-negative values: the smallest r with
  // sum(0, r) >= target, or size() + 1 if there is none. O(log n).
  size_t lower_bound(int64_t target) const;

 private:
  std::vector<int64_t> p_;  // p_[i] = a[0] + ... + a[i-1]
};

}  // namespace course
