#include "repl/backlog/backlog.h"

#include <gtest/gtest.h>

#include <string>

#include "common/rng.h"

namespace rd {
namespace {

TEST(Backlog, Empty) {
  Backlog b(16);
  EXPECT_EQ(b.offset(), 0u);
  EXPECT_EQ(b.size(), 0u);
  EXPECT_EQ(b.since(1), std::nullopt) << "nothing was ever sent: full resync";
}

TEST(Backlog, OffsetsCountBytesFromOne) {
  Backlog b(100);
  b.append("hello");
  EXPECT_EQ(b.offset(), 5u);
  EXPECT_EQ(b.first_offset(), 1u);
  EXPECT_EQ(b.since(1), "hello");
  EXPECT_EQ(b.since(3), "llo");
  EXPECT_EQ(b.since(6), "") << "a replica at offset 5 is up to date";
  EXPECT_EQ(b.since(7), std::nullopt) << "a replica claiming bytes from the future";
}

TEST(Backlog, ForgetsTheOldest) {
  Backlog b(8);
  b.append("abcdef");
  b.append("ghij");  // stream: abcdefghij, offsets 1..10; kept: cdefghij
  EXPECT_EQ(b.offset(), 10u);
  EXPECT_EQ(b.size(), 8u);
  EXPECT_EQ(b.first_offset(), 3u);
  EXPECT_EQ(b.since(3), "cdefghij");
  EXPECT_EQ(b.since(2), std::nullopt) << "byte 2 is gone: full resync";
  EXPECT_EQ(b.since(9), "ij");
}

// The pitfall: a single append bigger than the whole ring.
TEST(Backlog, HugeAppendKeepsOnlyTheTail) {
  Backlog b(4);
  b.append("x");
  b.append("0123456789");
  EXPECT_EQ(b.offset(), 11u);
  EXPECT_EQ(b.since(8), "6789");
  EXPECT_EQ(b.since(7), std::nullopt);
}

// The oracle: the whole stream as a std::string.
TEST(Backlog, MatchesTheWholeStream) {
  Rng rng(31);
  for (size_t cap : {1u, 7u, 64u, 1000u}) {
    Backlog b(cap);
    std::string stream;
    for (int i = 0; i < 2000; ++i) {
      std::string chunk(rng.below(rng.coin(0.1) ? 3000 : 30), '\0');
      for (char& c : chunk) c = static_cast<char>('a' + rng.below(26));
      b.append(chunk);
      stream += chunk;
      ASSERT_EQ(b.offset(), stream.size());
      ASSERT_EQ(b.size(), std::min(cap, stream.size()));
      uint64_t next = 1 + rng.below(stream.size() + 2);
      auto got = b.since(next);
      bool kept = next >= stream.size() - b.size() + 1 && next <= stream.size() + 1;
      ASSERT_EQ(got.has_value(), kept) << "cap " << cap << " next " << next;
      if (kept) ASSERT_EQ(*got, stream.substr(next - 1)) << "cap " << cap << " next " << next;
    }
  }
}

}  // namespace
}  // namespace rd
