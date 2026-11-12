#include "ds/listpack/listpack.h"

#include "common/check.h"
#include "resp/parser/parser.h"

namespace rd {

namespace {

constexpr size_t kHeader = 6;

uint32_t get_le(std::string_view b, size_t at, int n) {
  uint32_t v = 0;
  for (int i = n - 1; i >= 0; --i) v = (v << 8) | static_cast<unsigned char>(b[at + static_cast<size_t>(i)]);
  return v;
}

void put_le(std::string& b, size_t at, uint64_t v, int n) {
  for (int i = 0; i < n; ++i) b[at + static_cast<size_t>(i)] = static_cast<char>((v >> (8 * i)) & 0xff);
}

void append_le(std::string& out, uint64_t v, int n) {
  for (int i = 0; i < n; ++i) out += static_cast<char>((v >> (8 * i)) & 0xff);
}

std::string encode(std::string_view value) {
  std::string out;
  int64_t v;
  if (resp::parse_int64(value, &v)) {
    if (v >= 0 && v <= 127) {
      out += static_cast<char>(v);
    } else if (v >= -4096 && v <= 4095) {
      uint64_t u = v < 0 ? (uint64_t{1} << 13) + static_cast<uint64_t>(v) : static_cast<uint64_t>(v);
      out += static_cast<char>(0xC0 | (u >> 8));
      out += static_cast<char>(u & 0xff);
    } else if (v >= -32768 && v <= 32767) {
      out += '\xF1';
      append_le(out, static_cast<uint64_t>(v), 2);
    } else if (v >= -8388608 && v <= 8388607) {
      out += '\xF2';
      append_le(out, static_cast<uint64_t>(v), 3);
    } else if (v >= -2147483648LL && v <= 2147483647LL) {
      out += '\xF3';
      append_le(out, static_cast<uint64_t>(v), 4);
    } else {
      out += '\xF4';
      append_le(out, static_cast<uint64_t>(v), 8);
    }
    return out;
  }
  size_t n = value.size();
  if (n < 64) {
    out += static_cast<char>(0x80 | n);
  } else if (n < 4096) {
    out += static_cast<char>(0xE0 | (n >> 8));
    out += static_cast<char>(n & 0xff);
  } else {
    out += '\xF0';
    append_le(out, n, 4);
  }
  out += value;
  return out;
}

std::string backlen(size_t l) {
  std::string out;
  if (l <= 127) {
    out += static_cast<char>(l);
    return out;
  }
  std::vector<unsigned char> groups;
  while (l > 0) {
    groups.push_back(static_cast<unsigned char>(l & 127));
    l >>= 7;
  }
  for (size_t i = groups.size(); i-- > 0;) out += static_cast<char>(i == groups.size() - 1 ? groups[i] : (groups[i] | 128));
  return out;
}

size_t backlen_size(size_t l) {
  size_t n = 1;
  while (l > 127) {
    l >>= 7;
    ++n;
  }
  return n;
}

// Decodes the entry at `at`: its value and the size of encoding+data. False if it runs
// past `end` or the encoding is invalid.
bool decode(std::string_view b, size_t at, size_t end, std::string* value, size_t* len) {
  if (at >= end) return false;
  auto c = static_cast<unsigned char>(b[at]);
  auto need = [&](size_t n) { return at + n <= end; };
  int64_t iv;
  if ((c & 0x80) == 0) {
    *len = 1;
    iv = c;
  } else if ((c & 0xC0) == 0x80) {
    size_t n = c & 0x3F;
    if (!need(1 + n)) return false;
    *value = std::string(b.substr(at + 1, n));
    *len = 1 + n;
    return true;
  } else if ((c & 0xE0) == 0xC0) {
    if (!need(2)) return false;
    uint64_t u = (static_cast<uint64_t>(c & 0x1F) << 8) | static_cast<unsigned char>(b[at + 1]);
    iv = u >= 4096 ? static_cast<int64_t>(u) - 8192 : static_cast<int64_t>(u);
    *len = 2;
  } else if ((c & 0xF0) == 0xE0) {
    if (!need(2)) return false;
    size_t n = (static_cast<size_t>(c & 0x0F) << 8) | static_cast<unsigned char>(b[at + 1]);
    if (!need(2 + n)) return false;
    *value = std::string(b.substr(at + 2, n));
    *len = 2 + n;
    return true;
  } else if (c == 0xF0) {
    if (!need(5)) return false;
    size_t n = get_le(b, at + 1, 4);
    if (!need(5 + n)) return false;
    *value = std::string(b.substr(at + 5, n));
    *len = 5 + n;
    return true;
  } else if (c >= 0xF1 && c <= 0xF4) {
    int w = c == 0xF1 ? 2 : c == 0xF2 ? 3 : c == 0xF3 ? 4 : 8;
    if (!need(1 + static_cast<size_t>(w))) return false;
    uint64_t u = 0;
    for (int i = w - 1; i >= 0; --i) u = (u << 8) | static_cast<unsigned char>(b[at + 1 + static_cast<size_t>(i)]);
    int shift = 64 - 8 * w;
    iv = static_cast<int64_t>(u << shift) >> shift;
    *len = 1 + static_cast<size_t>(w);
  } else {
    return false;
  }
  *value = std::to_string(iv);
  return true;
}

// The backlen ending just before `end` (exclusive): the entry length and how many bytes
// it used. False if malformed.
bool read_backlen(std::string_view b, size_t end, size_t floor, size_t* l, size_t* used) {
  size_t v = 0;
  int shift = 0;
  size_t i = end;
  for (size_t n = 0; n < 5; ++n) {
    if (i <= floor) return false;
    auto c = static_cast<unsigned char>(b[--i]);
    v |= static_cast<size_t>(c & 127) << shift;
    shift += 7;
    if (!(c & 128)) {
      *l = v;
      *used = end - i;
      return true;
    }
  }
  return false;
}

// Byte offset of entry `index` (or of the terminator when index == size).
size_t offset_of(std::string_view b, size_t index) {
  size_t at = kHeader;
  std::string v;
  for (size_t i = 0; i < index; ++i) {
    size_t len;
    RD_CHECK(decode(b, at, b.size() - 1, &v, &len));
    at += len + backlen_size(len);
  }
  return at;
}

void set_header(std::string& b, size_t count) {
  put_le(b, 0, b.size(), 4);
  put_le(b, 4, count >= 65535 ? 65535 : count, 2);
}

}  // namespace

Listpack::Listpack() : blob_(7, '\0') {
  blob_[6] = '\xFF';
  set_header(blob_, 0);
}

size_t Listpack::size() const {
  size_t n = get_le(blob_, 4, 2);
  if (n < 65535) return n;
  return to_vector().size();
}

void Listpack::push_back(std::string_view value) { insert(size(), value); }

void Listpack::insert(size_t index, std::string_view value) {
  size_t n = size();
  RD_CHECK_LE(index, n);
  std::string e = encode(value);
  e += backlen(e.size());
  blob_.insert(offset_of(blob_, index), e);
  set_header(blob_, n + 1);
}

void Listpack::erase(size_t index) {
  size_t n = size();
  RD_CHECK_LT(index, n);
  size_t at = offset_of(blob_, index);
  std::string v;
  size_t len;
  RD_CHECK(decode(blob_, at, blob_.size() - 1, &v, &len));
  blob_.erase(at, len + backlen_size(len));
  set_header(blob_, n - 1);
}

std::string Listpack::get(size_t index) const {
  RD_CHECK_LT(index, size());
  std::string v;
  size_t len;
  RD_CHECK(decode(blob_, offset_of(blob_, index), blob_.size() - 1, &v, &len));
  return v;
}

std::optional<size_t> Listpack::find(std::string_view value) const {
  auto all = to_vector();
  for (size_t i = 0; i < all.size(); ++i) {
    if (all[i] == value) return i;
  }
  return std::nullopt;
}

std::vector<std::string> Listpack::to_vector() const {
  std::vector<std::string> out;
  size_t at = kHeader;
  while (static_cast<unsigned char>(blob_[at]) != 0xFF) {
    std::string v;
    size_t len;
    RD_CHECK(decode(blob_, at, blob_.size() - 1, &v, &len));
    out.push_back(std::move(v));
    at += len + backlen_size(len);
  }
  return out;
}

std::vector<std::string> Listpack::to_vector_reversed() const {
  std::vector<std::string> out;
  size_t end = blob_.size() - 1;  // the terminator
  while (end > kHeader) {
    size_t l, used;
    RD_CHECK(read_backlen(blob_, end, kHeader, &l, &used));
    size_t start = end - used - l;
    std::string v;
    size_t len;
    RD_CHECK(decode(blob_, start, end - used, &v, &len));
    out.push_back(std::move(v));
    end = start;
  }
  return out;
}

std::optional<Listpack> Listpack::from_bytes(std::string_view b) {
  if (b.size() < 7 || get_le(b, 0, 4) != b.size() || static_cast<unsigned char>(b.back()) != 0xFF) return std::nullopt;
  size_t at = kHeader, count = 0;
  while (at < b.size() - 1) {
    std::string v;
    size_t len, l, used;
    if (!decode(b, at, b.size() - 1, &v, &len)) return std::nullopt;
    size_t end = at + len + backlen_size(len);
    if (end > b.size() - 1 || !read_backlen(b, end, at + len, &l, &used) || l != len) return std::nullopt;
    at = end;
    ++count;
  }
  if (at != b.size() - 1) return std::nullopt;
  size_t header_count = get_le(b, 4, 2);
  if (header_count != 65535 && header_count != count) return std::nullopt;
  Listpack lp;
  lp.blob_ = std::string(b);
  return lp;
}

}  // namespace rd
