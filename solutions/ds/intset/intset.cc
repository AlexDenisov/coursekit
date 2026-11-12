#include "ds/intset/intset.h"

#include <cstring>
#include <limits>

#include "common/check.h"

namespace rd {

namespace {

uint32_t get_u32(const std::string& b, size_t at) {
  uint32_t v = 0;
  for (int i = 3; i >= 0; --i) v = (v << 8) | static_cast<unsigned char>(b[at + i]);
  return v;
}

void put_u32(std::string& b, size_t at, uint32_t v) {
  for (int i = 0; i < 4; ++i) b[at + i] = static_cast<char>((v >> (8 * i)) & 0xff);
}

int64_t get_int(const std::string& b, size_t at, int enc) {
  uint64_t v = 0;
  for (int i = enc - 1; i >= 0; --i) v = (v << 8) | static_cast<unsigned char>(b[at + i]);
  int shift = 64 - 8 * enc;
  return static_cast<int64_t>(v << shift) >> shift;
}

void put_int(std::string& b, size_t at, int enc, int64_t v) {
  auto u = static_cast<uint64_t>(v);
  for (int i = 0; i < enc; ++i) b[at + i] = static_cast<char>((u >> (8 * i)) & 0xff);
}

int needed(int64_t v) {
  if (v >= std::numeric_limits<int16_t>::min() && v <= std::numeric_limits<int16_t>::max()) return 2;
  if (v >= std::numeric_limits<int32_t>::min() && v <= std::numeric_limits<int32_t>::max()) return 4;
  return 8;
}

}  // namespace

IntSet::IntSet() : blob_(8, '\0') { put_u32(blob_, 0, 2); }

size_t IntSet::size() const { return get_u32(blob_, 4); }
int IntSet::encoding() const { return static_cast<int>(get_u32(blob_, 0)); }

int64_t IntSet::at(size_t i) const {
  RD_CHECK_LT(i, size());
  return get_int(blob_, 8 + i * static_cast<size_t>(encoding()), encoding());
}

// Binary search: true and the index if found, else false and the insertion point.
static bool search(const IntSet& s, int64_t v, size_t* pos) {
  size_t lo = 0, hi = s.size();
  while (lo < hi) {
    size_t mid = (lo + hi) / 2;
    int64_t x = s.at(mid);
    if (x == v) {
      *pos = mid;
      return true;
    }
    if (x < v) {
      lo = mid + 1;
    } else {
      hi = mid;
    }
  }
  *pos = lo;
  return false;
}

bool IntSet::contains(int64_t v) const {
  if (needed(v) > encoding()) return false;
  size_t pos;
  return search(*this, v, &pos);
}

bool IntSet::add(int64_t v) {
  int enc = encoding();
  size_t n = size();
  if (needed(v) > enc) {
    int nenc = needed(v);
    std::string nb(8 + (n + 1) * static_cast<size_t>(nenc), '\0');
    put_u32(nb, 0, static_cast<uint32_t>(nenc));
    put_u32(nb, 4, static_cast<uint32_t>(n + 1));
    // v is out of the old range, so it's either below or above every element.
    size_t off = v < 0 ? 1 : 0;
    for (size_t i = 0; i < n; ++i) put_int(nb, 8 + (i + off) * nenc, nenc, get_int(blob_, 8 + i * enc, enc));
    put_int(nb, 8 + (v < 0 ? 0 : n) * nenc, nenc, v);
    blob_ = std::move(nb);
    return true;
  }
  size_t pos;
  if (search(*this, v, &pos)) return false;
  blob_.insert(8 + pos * static_cast<size_t>(enc), static_cast<size_t>(enc), '\0');
  put_int(blob_, 8 + pos * enc, enc, v);
  put_u32(blob_, 4, static_cast<uint32_t>(n + 1));
  return true;
}

bool IntSet::remove(int64_t v) {
  size_t pos;
  if (needed(v) > encoding() || !search(*this, v, &pos)) return false;
  blob_.erase(8 + pos * static_cast<size_t>(encoding()), static_cast<size_t>(encoding()));
  put_u32(blob_, 4, static_cast<uint32_t>(size() - 1));
  return true;
}

std::optional<IntSet> IntSet::from_bytes(std::string_view b) {
  if (b.size() < 8) return std::nullopt;
  IntSet s;
  s.blob_ = std::string(b);
  int enc = s.encoding();
  if (enc != 2 && enc != 4 && enc != 8) return std::nullopt;
  if (b.size() != 8 + s.size() * static_cast<size_t>(enc)) return std::nullopt;
  for (size_t i = 1; i < s.size(); ++i) {
    if (s.at(i - 1) >= s.at(i)) return std::nullopt;
  }
  return s;
}

}  // namespace rd
