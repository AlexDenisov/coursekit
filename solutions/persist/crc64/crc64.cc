#include "persist/crc64/crc64.h"

#include <array>

namespace rd {

namespace {

constexpr uint64_t kPoly = 0x95ac9329ac4bc9b5ull;

constexpr std::array<uint64_t, 256> make_table() {
  std::array<uint64_t, 256> t{};
  for (uint64_t b = 0; b < 256; ++b) {
    uint64_t crc = b;
    for (int i = 0; i < 8; ++i) crc = (crc & 1) ? (crc >> 1) ^ kPoly : crc >> 1;
    t[b] = crc;
  }
  return t;
}

constexpr std::array<uint64_t, 256> kTable = make_table();

}  // namespace

uint64_t crc64(uint64_t crc, std::string_view data) {
  for (unsigned char b : data) {
    crc ^= b;
    for (int i = 0; i < 8; ++i) crc = (crc & 1) ? (crc >> 1) ^ kPoly : crc >> 1;
  }
  return crc;
}

uint64_t crc64_fast(uint64_t crc, std::string_view data) {
  for (unsigned char b : data) crc = kTable[(crc ^ b) & 0xff] ^ (crc >> 8);
  return crc;
}

}  // namespace rd
