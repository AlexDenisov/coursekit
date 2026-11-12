#pragma once

// persist/crc64: the checksum at the end of every RDB file and DUMP payload. The story
// is README.md.

#include <cstdint>
#include <string_view>

namespace rd {

// Redis's CRC-64: the Jones polynomial 0xad93d23594c935a9, reflected (bit 0 of each
// byte first, so shift right and use the bit-reversed polynomial 0x95ac9329ac4bc9b5),
// initial value 0, no final xor. Continues from `crc`, so
//   crc64(crc64(0, a), b) == crc64(0, a + b).
// crc64(0, "123456789") == 0xe9c6d914c4b8d9ca. Bit by bit: O(8 |data|).
uint64_t crc64(uint64_t crc, std::string_view data);

// Optional. The same function, a byte at a time from a 256-entry table: O(|data|) with
// a much smaller constant. persist/rdb_writer checksums whole datasets with it.
uint64_t crc64_fast(uint64_t crc, std::string_view data);

}  // namespace rd
