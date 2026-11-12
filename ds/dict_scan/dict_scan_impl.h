#pragma once

// Your implementation of dict_scan (a template, so it lives in a header). Included at
// the end of dict_scan.h.

namespace rd {

template <class V, class F>
uint64_t dict_scan(const Dict<V>& d, uint64_t cursor, F&& fn) {
  // TODO(you): the two cases from dict_scan.h, using d.bucket_count(t),
  // d.is_rehashing() and d.for_each_in_bucket(t, index, fn).
  return 0;
}

}  // namespace rd
