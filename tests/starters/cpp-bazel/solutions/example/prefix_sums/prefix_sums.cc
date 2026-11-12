#include "example/prefix_sums/prefix_sums.h"

#include <algorithm>

#include "common/check.h"

namespace course {

PrefixSums::PrefixSums(std::span<const int32_t> a) {
  p_.reserve(a.size() + 1);
  p_.push_back(0);
  for (int32_t x : a) p_.push_back(p_.back() + x);
}

int64_t PrefixSums::sum(size_t l, size_t r) const {
  COURSE_CHECK_LE(l, r);
  COURSE_CHECK_LE(r, size());
  return p_[r] - p_[l];
}

size_t PrefixSums::lower_bound(int64_t target) const {
  return static_cast<size_t>(std::lower_bound(p_.begin(), p_.end(), target) - p_.begin());
}

}  // namespace course
