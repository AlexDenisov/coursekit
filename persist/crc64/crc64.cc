#include "persist/crc64/crc64.h"

namespace rd {

uint64_t crc64(uint64_t crc, std::string_view data) {
  // TODO(you): for each byte, xor it into the low bits, then 8 times: shift right,
  // and xor in the reflected polynomial if the bit shifted out was 1.
  return 0;
}

uint64_t crc64_fast(uint64_t crc, std::string_view data) {
  // TODO(you, optional): table[b] = the 8 shift steps applied to b alone; then per
  // byte: crc = table[(crc ^ byte) & 0xff] ^ (crc >> 8).
  return 0;
}

}  // namespace rd
