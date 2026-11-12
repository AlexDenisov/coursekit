#include "ds/dict_scan/dict_scan.h"

namespace rd {

namespace {

uint64_t reverse_bits(uint64_t v) {
  uint64_t r = 0;
  for (int i = 0; i < 64; ++i) {
    r = (r << 1) | (v & 1);
    v >>= 1;
  }
  return r;
}

}  // namespace

uint64_t scan_next_cursor(uint64_t cursor, uint64_t mask) {
  cursor |= ~mask;
  cursor = reverse_bits(cursor);
  ++cursor;
  return reverse_bits(cursor);
}

}  // namespace rd
