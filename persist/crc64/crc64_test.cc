#include "persist/crc64/crc64.h"

#include <gtest/gtest.h>

#include <string>

// Values from `python3 tools/crc64_reference.py`: a bit-by-bit implementation of the
// definition, cross-checked against the CRC Redis itself appends to DUMP payloads.

namespace rd {
namespace {

// A string literal's bytes, embedded zeros included.
template <size_t N>
std::string bytes(const char (&lit)[N]) {
  return std::string(lit, N - 1);
}

std::string all_bytes() {
  std::string s;
  for (int i = 0; i < 256; ++i) s += static_cast<char>(i);
  return s;
}

TEST(Crc64, PublishedCheckValue) {
  EXPECT_EQ(crc64(0, "123456789"), 0xe9c6d914c4b8d9caull);
}

TEST(Crc64, KnownStrings) {
  EXPECT_EQ(crc64(0, ""), 0x0000000000000000ull);
  EXPECT_EQ(crc64(0, "a"), 0x5fb354025b277b14ull);
  EXPECT_EQ(crc64(0, "hello world"), 0x50dced42e0e1e618ull);
  EXPECT_EQ(crc64(0, all_bytes()), 0x88bfa574e806500eull);
}

// A property of CRCs with initial value 0: zero bytes leave a zero CRC unchanged.
// (That's why many CRC variants start from all ones. Redis's doesn't.)
TEST(Crc64, ZerosFromZeroStayZero) {
  EXPECT_EQ(crc64(0, std::string(1000, '\0')), 0ull);
}

TEST(Crc64, ContinuesFromAPreviousValue) {
  std::string a = "hello ", b = "world";
  EXPECT_EQ(crc64(crc64(0, a), b), crc64(0, a + b));
  std::string all = all_bytes();
  uint64_t crc = 0;
  for (char c : all) crc = crc64(crc, std::string_view(&c, 1));
  EXPECT_EQ(crc, 0x88bfa574e806500eull);
}

// Real Redis: DUMP <key> returns <payload><8-byte little-endian CRC-64 of payload>.
TEST(Crc64, MatchesRedisDumpChecksums) {
  struct Case {
    std::string body;
    uint64_t crc;
  };
  const Case cases[] = {
      {bytes("\000\005hello\017\000"), 0xf2dd98cb7abaf678ull},  // SET k hello
      {bytes("\000\30190\017\000"), 0x41f5503d2b8f1086ull},     // SET n 12345
      {bytes("\022\001\002\020\020\000\000\000\003\000\201a\002\201b\002\201c\002\377\017\000"),
       0x66af9f3b00dc9cd3ull},  // RPUSH l a b c
      {bytes("\020\r\r\000\000\000\002\000\201f\002\201v\002\377\017\000"),
       0x65f5f88d4858753bull},  // HSET h f v
  };
  for (const Case& c : cases) {
    EXPECT_EQ(crc64(0, c.body), c.crc) << ::testing::PrintToString(c.body);
  }
}

}  // namespace
}  // namespace rd
