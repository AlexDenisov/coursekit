#pragma once

// ds/dict_scan: SCAN's cursor (dict.c's dictScan). The spec is this file; templates go in
// dict_scan_impl.h; the story is README.md.

#include <cstdint>

#include "ds/dict/dict.h"

namespace rd {

// The cursor after `cursor` for a table with `mask` = buckets - 1: increment the
// *reversed* bits of cursor within the mask (set the bits above the mask first, so the
// carry runs off the end). For mask 7: 0 -> 4 -> 2 -> 6 -> 1 -> 5 -> 3 -> 7 -> 0.
uint64_t scan_next_cursor(uint64_t cursor, uint64_t mask);

// One step of a scan: calls fn(key, value) for the entries in the bucket(s) `cursor`
// names and returns the next cursor; 0 means the scan is complete. Start with 0.
//   - Not rehashing: the bucket cursor & mask of table 0; then scan_next_cursor.
//   - Rehashing: let s be the smaller table and l the larger. Emit s's bucket
//     cursor & mask_s; then every bucket of l whose index is an expansion of it:
//     repeatedly emit l's bucket cursor & mask_l and advance cursor with mask_l, while
//     cursor has any bits set in (mask_s ^ mask_l).
// Guarantee: every key present for the whole duration of a scan (from cursor 0 back to
// 0) is returned at least once, however much the dict grows, shrinks or rehashes between
// the calls. Keys may be returned more than once. An empty dict returns 0 at once.
template <class V, class F>
uint64_t dict_scan(const Dict<V>& d, uint64_t cursor, F&& fn);

}  // namespace rd

#include "ds/dict_scan/dict_scan_impl.h"
