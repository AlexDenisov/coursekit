#include "example/prefix_sums/prefix_sums.h"

#include "common/check.h"

namespace course {

PrefixSums::PrefixSums(std::span<const int32_t> a) {
  // TODO(you): fill p_ with a.size() + 1 running totals, starting at 0.
}

int64_t PrefixSums::sum(size_t l, size_t r) const {
  COURSE_CHECK_LE(l, r);
  COURSE_CHECK_LE(r, size());
  // TODO(you): one subtraction.
  return 0;
}

size_t PrefixSums::lower_bound(int64_t target) const {
  // TODO(you, optional): p_ is non-decreasing; binary search it.
  return 0;
}

}  // namespace course
