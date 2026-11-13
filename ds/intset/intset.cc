#include "ds/intset/intset.h"

#include "common/check.h"

namespace rd {

IntSet::IntSet() {
  // TODO(you): the 8-byte header for an empty set with encoding 2.
}

size_t IntSet::size() const {
  // TODO(you): read the length field.
  return 0;
}

int IntSet::encoding() const {
  // TODO(you): read the encoding field.
  return 0;
}

bool IntSet::contains(int64_t v) const {
  // TODO(you): out of the encoding's range? No. Else binary search.
  return false;
}

bool IntSet::add(int64_t v) {
  // TODO(you): upgrade if v needs a wider encoding (then v goes at one end: why?);
  // else binary-search the position, shift the tail, write v, bump the length.
  return false;
}

bool IntSet::remove(int64_t v) {
  // TODO(you): find, shift the tail down, shrink.
  return false;
}

int64_t IntSet::at(size_t i) const {
  RD_CHECK_LT(i, size());
  // TODO(you): read `encoding` bytes little-endian at 8 + i * encoding, sign-extended.
  return 0;
}

std::optional<IntSet> IntSet::from_bytes(std::string_view b) {
  // TODO(you): the checks, then adopt the bytes.
  return std::nullopt;
}

}  // namespace rd
