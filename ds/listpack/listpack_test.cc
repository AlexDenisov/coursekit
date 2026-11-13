#include "ds/listpack/listpack.h"

#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "common/rng.h"

// Blobs recorded from Redis 8.10 by `python3 tools/blob_reference.py` (DUMP of small
// hashes, lists and sets embeds their listpack as-is).

namespace rd {
namespace {

template <size_t N>
std::string bytes(const char (&lit)[N]) {
  return std::string(lit, N - 1);
}

// For blobs too long to paste: the size, both ends, and an FNV-1a hash of everything.
struct BigBlob {
  size_t size;
  std::string head, tail;
  uint64_t fnv;
};

uint64_t fnv1a(std::string_view s) {
  uint64_t h = 0xcbf29ce484222325ull;
  for (unsigned char c : s) h = (h ^ c) * 0x100000001b3ull;
  return h;
}

void expect_blob(std::string_view got, const BigBlob& want) {
  ASSERT_EQ(got.size(), want.size);
  EXPECT_EQ(got.substr(0, want.head.size()), want.head);
  EXPECT_EQ(got.substr(got.size() - want.tail.size()), want.tail);
  EXPECT_EQ(fnv1a(got), want.fnv);
}

Listpack make(std::vector<std::string> values) {
  Listpack lp;
  for (auto& v : values) lp.push_back(v);
  return lp;
}

TEST(Listpack, Empty) {
  Listpack lp;
  EXPECT_EQ(lp.size(), 0u);
  EXPECT_EQ(lp.bytes(), bytes("\007\000\000\000\000\000\377"));
}

// HSET k f v
TEST(Listpack, SmallStrings) {
  EXPECT_EQ(make({"f", "v"}).bytes(), bytes("\r\000\000\000\002\000\201f\002\201v\002\377"));
}

// HSET k a 0 b 127 c 128 d -1 e 4095 f -4096 g 4096
TEST(Listpack, SmallIntegers) {
  EXPECT_EQ(make({"a", "0", "b", "127", "c", "128", "d", "-1", "e", "4095", "f", "-4096", "g", "4096"}).bytes(),
            bytes("0\000\000\000\016\000\201a\002\000\001\201b\002\177\001\201c\002\300\200\002\201d\002\337\377"
                  "\002\201e\002\317\377\002\201f\002\320\000\002\201g\002\361\000\020\003\377"));
}

// HSET k a 32767 b -32768 c 32768 d 8388607 e -8388609 f 2147483648 g -9223372036854775808
TEST(Listpack, WideIntegers) {
  EXPECT_EQ(make({"a", "32767", "b", "-32768", "c", "32768", "d", "8388607", "e", "-8388609", "f", "2147483648", "g",
                  "-9223372036854775808"})
                .bytes(),
            bytes("H\000\000\000\016\000\201a\002\361\377\177\003\201b\002\361\000\200\003\201c\002\362\000\200\000"
                  "\004\201d\002\362\377\377\177\004\201e\002\363\377\377\177\377\005\201f\002\364\000\000\000\200\000"
                  "\000\000\000\011\201g\002\364\000\000\000\000\000\000\000\200\011\377"));
}

// The pitfall: "looks like a number" is decided by parse_int64's strict rules. "01",
// "-0", "+1", "1.5" and 2^63 are strings.
// HSET k a 01 b -0 c +1 d 1.5 e 9223372036854775808
TEST(Listpack, IntegerLikeStringsStayStrings) {
  EXPECT_EQ(make({"a", "01", "b", "-0", "c", "+1", "d", "1.5", "e", "9223372036854775808"}).bytes(),
            bytes("<\000\000\000\n\000\201a\002\20201\003\201b\002\202-0\003\201c\002\202+1\003\201d\002\2031.5\004"
                  "\201e\002\2239223372036854775808\024\377"));
}

// RPUSH k a <63 x> <64 y> ""
TEST(Listpack, StringLengthBoundaries) {
  EXPECT_EQ(make({"a", std::string(63, 'x'), std::string(64, 'y'), ""}).bytes(),
            bytes("\220\000\000\000\004\000\201a\002\277xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx"
                  "@\340@yyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyB\200\001\377"));
}

TEST(Listpack, LongStrings) {
  // RPUSH k <4095 z>: the largest 12-bit length; RPUSH k <4096 w>: 32-bit length
  expect_blob(make({std::string(4095, 'z')}).bytes(),
              {4106, bytes("\n\020\000\000\001\000\357\377zzzzzzzz"), bytes("zzzzzzzzzzzzz \201\377"), 0xd51f1ac479c3ee42ull});
  expect_blob(make({std::string(4096, 'w')}).bytes(),
              {4110, bytes("\016\020\000\000\001\000\360\000\020\000\000wwwww"), bytes("wwwwwwwwwwwww \205\377"),
               0xec1dd60f6f8e25e6ull});
}

// RPUSH k <125 b> <126 c> <300 d>: entries of 127 and 128 bytes straddle the 1-byte
// backlen limit.
TEST(Listpack, BacklenBoundary) {
  expect_blob(make({std::string(125, 'b'), std::string(126, 'c'), std::string(300, 'd')}).bytes(),
              {569, bytes("9\002\000\000\003\000\340}bbbbbbbb"), bytes("ddddddddddddd\002\256\377"), 0xa010ba350d807a66ull});
}

TEST(Listpack, GetDecodesIntegersBackToDecimal) {
  Listpack lp = make({"-4096", "4096", "9223372036854775807", "x", "128"});
  EXPECT_EQ(lp.get(0), "-4096");
  EXPECT_EQ(lp.get(1), "4096");
  EXPECT_EQ(lp.get(2), "9223372036854775807");
  EXPECT_EQ(lp.get(3), "x");
  EXPECT_EQ(lp.find("128"), 4u);
  EXPECT_EQ(lp.find("nope"), std::nullopt);
}

TEST(Listpack, InsertAndErase) {
  Listpack lp = make({"a", "c"});
  lp.insert(1, "b");
  lp.insert(0, "start");
  lp.insert(4, "end");
  EXPECT_EQ(lp.to_vector(), (std::vector<std::string>{"start", "a", "b", "c", "end"}));
  lp.erase(0);
  lp.erase(3);
  EXPECT_EQ(lp.to_vector(), (std::vector<std::string>{"a", "b", "c"}));
  EXPECT_EQ(lp.bytes(), make({"a", "b", "c"}).bytes());
}

// SADD k a b c
TEST(Listpack, WalksBackwards) {
  Listpack lp = make({"a", std::string(200, 'b'), "300", std::string(5000, 'c'), ""});
  EXPECT_EQ(lp.to_vector_reversed(), (std::vector<std::string>{"", std::string(5000, 'c'), "300", std::string(200, 'b'), "a"}));
  EXPECT_EQ(make({"a", "b", "c"}).bytes(), bytes("\020\000\000\000\003\000\201a\002\201b\002\201c\002\377"));
}

TEST(Listpack, FromBytes) {
  Listpack lp = make({"x", "42", std::string(100, 'y')});
  auto back = Listpack::from_bytes(lp.bytes());
  ASSERT_TRUE(back.has_value());
  EXPECT_EQ(back->to_vector(), lp.to_vector());
  std::string bad(lp.bytes());
  bad[0] = 99;  // wrong total
  EXPECT_FALSE(Listpack::from_bytes(bad));
  bad = std::string(lp.bytes());
  bad.back() = 0;  // no terminator
  EXPECT_FALSE(Listpack::from_bytes(bad));
  bad = std::string(lp.bytes());
  bad[4] = 7;  // wrong count
  EXPECT_FALSE(Listpack::from_bytes(bad));
  EXPECT_FALSE(Listpack::from_bytes(""));
}

// The oracle: std::vector<std::string>; and every intermediate blob must validate.
TEST(Listpack, MatchesAModel) {
  Rng rng(23);
  Listpack lp;
  std::vector<std::string> model;
  auto random_value = [&] {
    switch (rng.below(4)) {
      case 0: return std::to_string(rng.between(-200000, 200000));
      case 1: return std::to_string(static_cast<int64_t>(rng.next()));
      case 2: return std::string(rng.below(70), 's');
      default: return std::string(rng.below(5000), 'L');
    }
  };
  for (int step = 0; step < 2000; ++step) {
    if (rng.coin(0.6) || model.empty()) {
      size_t at = rng.below(model.size() + 1);
      std::string v = random_value();
      lp.insert(at, v);
      model.insert(model.begin() + static_cast<long>(at), v);
    } else {
      size_t at = rng.below(model.size());
      lp.erase(at);
      model.erase(model.begin() + static_cast<long>(at));
    }
    ASSERT_EQ(lp.size(), model.size());
    if (step % 25 == 0) {
      ASSERT_EQ(lp.to_vector(), model);
      std::vector<std::string> rev(model.rbegin(), model.rend());
      ASSERT_EQ(lp.to_vector_reversed(), rev);
      ASSERT_TRUE(Listpack::from_bytes(lp.bytes()).has_value()) << "step " << step;
    }
  }
}

}  // namespace
}  // namespace rd
