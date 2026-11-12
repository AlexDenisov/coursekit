#pragma once

// Your implementation of dict_scan (a template, so it lives in a header). Included at
// the end of dict_scan.h.

namespace rd {

template <class V, class F>
uint64_t dict_scan(const Dict<V>& d, uint64_t cursor, F&& fn) {
  if (d.size() == 0) return 0;
  if (!d.is_rehashing()) {
    uint64_t m0 = d.bucket_count(0) - 1;
    d.for_each_in_bucket(0, cursor & m0, fn);
    return scan_next_cursor(cursor, m0);
  }
  int small = d.bucket_count(0) <= d.bucket_count(1) ? 0 : 1;
  int large = 1 - small;
  uint64_t m0 = d.bucket_count(small) - 1;
  uint64_t m1 = d.bucket_count(large) - 1;
  d.for_each_in_bucket(small, cursor & m0, fn);
  do {
    d.for_each_in_bucket(large, cursor & m1, fn);
    cursor = scan_next_cursor(cursor, m1);
  } while (cursor & (m0 ^ m1));
  return cursor;
}

}  // namespace rd
