#pragma once

// ds/listpack: Redis's listpack.c, many small strings in one allocation. The spec is
// this file; the story is README.md.

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace rd {

// The bytes, exactly as Redis lays them out (and as RDB files embed them):
//
//   uint32 total bytes | uint16 element count | entry ... | 0xFF
//
// (little-endian; the count saturates at 65535, meaning "walk to count".) Each entry is
//   <encoding+data> <backlen>
// where backlen is the length of <encoding+data> written in 1..5 bytes so the entry can
// be found walking *backwards*: L <= 127 -> one byte L; else big-endian 7-bit groups,
// every byte but the first with its high bit set (L = 4097 -> 0x20 0x81).
//
// Encodings: a value that passes resp::parse_int64 is stored as an integer:
//   0..127                 0xxxxxxx                    (1 byte)
//   -4096..4095            110xxxxx xxxxxxxx           13-bit two's complement, high bits first
//   -32768..32767          0xF1 + 2 bytes LE
//   -2^23..2^23-1          0xF2 + 3 bytes LE
//   -2^31..2^31-1          0xF3 + 4 bytes LE
//   other int64            0xF4 + 8 bytes LE
// any other string of length n:
//   n < 64                 10xxxxxx + data             6-bit length
//   n < 4096               1110xxxx xxxxxxxx + data    12-bit length, high bits first
//   otherwise              0xF0 + 4-byte LE length + data
class Listpack {
 public:
  Listpack();  // empty: 7 bytes

  size_t size() const;
  std::string_view bytes() const { return blob_; }

  void push_back(std::string_view value);
  // Inserts before position index (index == size() appends). index <= size() (checked).
  void insert(size_t index, std::string_view value);
  // index < size() (checked).
  void erase(size_t index);
  // The element at index, integers rendered back in decimal. index < size() (checked).
  std::string get(size_t index) const;
  // The first index whose element equals value, or nullopt.
  std::optional<size_t> find(std::string_view value) const;

  // Every element front to back, walking forwards.
  std::vector<std::string> to_vector() const;
  // Every element back to front, walking backwards from the terminator using only the
  // backlens.
  std::vector<std::string> to_vector_reversed() const;

  // Validates and adopts a blob: the header's total matches; every entry's encoding and
  // backlen are consistent and inside the blob; the terminator is last; the count matches
  // (unless 65535). nullopt otherwise.
  static std::optional<Listpack> from_bytes(std::string_view b);

 private:
  std::string blob_;
};

}  // namespace rd
