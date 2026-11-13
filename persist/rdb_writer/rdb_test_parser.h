#pragma once

// A tiny, independent RDB parser for the tests: it understands exactly what
// write_snapshot is asked to produce (legacy types, int-encoded strings) and nothing
// more. It never calls the code under test.

#include <cstdint>
#include <cstring>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace rd::rdb::testing {

struct ParsedKey {
  int type = -1;
  std::vector<std::string> items;  // string: 1; list/set: elements; hash: f,v,...; zset: m,score,...
  std::optional<int64_t> expire_ms;
};

struct Parsed {
  std::string error;  // empty if fine
  std::string magic;
  std::map<std::string, ParsedKey> keys;
  uint64_t stored_crc = 0;
  size_t crc_offset = 0;  // bytes covered by the CRC
};

inline uint64_t crc64_reference(std::string_view data) {
  uint64_t crc = 0;
  for (unsigned char b : data) {
    crc ^= b;
    for (int i = 0; i < 8; ++i) crc = (crc & 1) ? (crc >> 1) ^ 0x95ac9329ac4bc9b5ull : crc >> 1;
  }
  return crc;
}

inline Parsed parse(std::string_view d) {
  Parsed p;
  size_t i = 0;
  auto fail = [&](std::string why) {
    p.error = why + " at offset " + std::to_string(i);
    return p;
  };
  auto need = [&](size_t n) { return i + n <= d.size(); };
  auto u8 = [&]() -> int { return need(1) ? static_cast<unsigned char>(d[i++]) : -1; };
  auto len = [&](bool* encoded) -> std::optional<uint64_t> {
    int b = u8();
    if (b < 0) return std::nullopt;
    *encoded = false;
    switch (b >> 6) {
      case 0: return b & 63;
      case 1: {
        int c = u8();
        if (c < 0) return std::nullopt;
        return ((b & 63) << 8) | c;
      }
      case 3: *encoded = true; return b & 63;
    }
    int n = b == 0x80 ? 4 : b == 0x81 ? 8 : 0;
    if (n == 0 || !need(static_cast<size_t>(n))) return std::nullopt;
    uint64_t v = 0;
    for (int k = 0; k < n; ++k) v = (v << 8) | static_cast<unsigned char>(d[i++]);
    return v;
  };
  auto str = [&]() -> std::optional<std::string> {
    bool enc;
    auto n = len(&enc);
    if (!n) return std::nullopt;
    if (enc) {
      int w = *n == 0 ? 1 : *n == 1 ? 2 : *n == 2 ? 4 : 0;
      if (w == 0 || !need(static_cast<size_t>(w))) return std::nullopt;
      uint64_t u = 0;
      for (int k = w - 1; k >= 0; --k) u = (u << 8) | static_cast<unsigned char>(d[i + static_cast<size_t>(k)]);
      i += static_cast<size_t>(w);
      int shift = 64 - 8 * w;
      return std::to_string(static_cast<int64_t>(u << shift) >> shift);
    }
    if (!need(*n)) return std::nullopt;
    std::string s(d.substr(i, *n));
    i += *n;
    return s;
  };
  if (d.size() < 9) return fail("too short");
  p.magic = std::string(d.substr(0, 9));
  i = 9;
  std::optional<int64_t> pending_expire;
  for (;;) {
    int op = u8();
    if (op < 0) return fail("truncated");
    bool enc;
    if (op == 0xFA) {
      if (!str() || !str()) return fail("bad aux");
    } else if (op == 0xFE) {
      if (!len(&enc)) return fail("bad selectdb");
    } else if (op == 0xFB) {
      if (!len(&enc) || !len(&enc)) return fail("bad resizedb");
    } else if (op == 0xFC) {
      if (!need(8)) return fail("bad expire");
      uint64_t v = 0;
      for (int k = 7; k >= 0; --k) v = (v << 8) | static_cast<unsigned char>(d[i + static_cast<size_t>(k)]);
      i += 8;
      pending_expire = static_cast<int64_t>(v);
    } else if (op == 0xFF) {
      p.crc_offset = i;
      if (!need(8)) return fail("no checksum");
      for (int k = 7; k >= 0; --k) p.stored_crc = (p.stored_crc << 8) | static_cast<unsigned char>(d[i + static_cast<size_t>(k)]);
      i += 8;
      if (i != d.size()) return fail("bytes after the checksum");
      return p;
    } else {
      auto key = str();
      if (!key) return fail("bad key");
      ParsedKey k;
      k.type = op;
      k.expire_ms = pending_expire;
      pending_expire.reset();
      if (op == 0) {
        auto v = str();
        if (!v) return fail("bad string value");
        k.items.push_back(*v);
      } else if (op == 1 || op == 2 || op == 4 || op == 5) {
        auto n = len(&enc);
        if (!n) return fail("bad length");
        uint64_t count = op == 4 ? 2 * *n : *n;
        for (uint64_t e = 0; e < count; ++e) {
          auto v = str();
          if (!v) return fail("bad element");
          k.items.push_back(*v);
          if (op == 5) {
            if (!need(8)) return fail("bad score");
            double s;
            std::memcpy(&s, d.data() + i, 8);  // little-endian hosts only, as macOS/arm64
            i += 8;
            k.items.push_back(std::to_string(s));
          }
        }
      } else {
        return fail("unexpected type " + std::to_string(op));
      }
      p.keys[*key] = std::move(k);
    }
  }
}

}  // namespace rd::rdb::testing
