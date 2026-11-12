#include <gtest/gtest.h>

#include <chrono>
#include <string>

#include "common/rng.h"
#include "persist/crc64/crc64.h"

namespace rd {
namespace {

TEST(Crc64Fast, PublishedCheckValue) {
  EXPECT_EQ(crc64_fast(0, "123456789"), 0xe9c6d914c4b8d9caull);
}

// The oracle: your bit-by-bit crc64.
TEST(Crc64Fast, MatchesBitByBitOnRandomData) {
  Rng rng(64);
  for (int i = 0; i < 500; ++i) {
    std::string s(rng.below(300), '\0');
    for (char& c : s) c = static_cast<char>(rng.below(256));
    uint64_t start = rng.coin() ? 0 : rng.next();
    ASSERT_EQ(crc64_fast(start, s), crc64(start, s)) << "length " << s.size();
  }
}

TEST(Crc64Fast, IsAtLeastFourTimesFaster) {
  std::string data(4 << 20, 'x');
  auto time = [&](auto f) {
    auto t0 = std::chrono::steady_clock::now();
    volatile uint64_t sink = f(0, data);
    (void)sink;
    return std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
  };
  double slow = time(crc64), fast = time(crc64_fast);
  EXPECT_LT(fast * 4, slow) << "bit by bit " << slow << " s, table " << fast << " s";
}

}  // namespace
}  // namespace rd
