#include "net/buffers/buffer.h"

#include <gtest/gtest.h>

#include <cstring>
#include <string>

#include "common/rng.h"

namespace rd {
namespace {

TEST(Buffer, StartsEmpty) {
  Buffer b;
  EXPECT_TRUE(b.empty());
  EXPECT_EQ(b.view(), "");
}

TEST(Buffer, AppendAndConsume) {
  Buffer b;
  b.append("hello ");
  b.append("world");
  EXPECT_EQ(b.size(), 11u);
  EXPECT_EQ(b.view(), "hello world");
  b.consume(6);
  EXPECT_EQ(b.view(), "world");
  b.consume(5);
  EXPECT_TRUE(b.empty());
}

TEST(Buffer, PrepareAndCommit) {
  Buffer b;
  b.append("ab");
  char* p = b.prepare(100);
  ASSERT_NE(p, nullptr);
  std::memcpy(p, "cdef", 4);
  b.commit(4);  // wrote fewer than prepared, like a short read()
  EXPECT_EQ(b.view(), "abcdef");
}

TEST(Buffer, BinaryData) {
  Buffer b;
  b.append(std::string_view("\0\r\n\0", 4));
  EXPECT_EQ(b.view(), std::string_view("\0\r\n\0", 4));
}

TEST(Buffer, Clear) {
  Buffer b;
  b.append("data");
  b.clear();
  EXPECT_TRUE(b.empty());
  b.append("x");
  EXPECT_EQ(b.view(), "x");
}

// The oracle: std::string, used as a (slow) queue.
TEST(Buffer, MatchesAStringModel) {
  Rng rng(9);
  Buffer b;
  std::string model;
  for (int i = 0; i < 20000; ++i) {
    if (rng.coin(0.55)) {
      std::string s(rng.below(300), '\0');
      for (char& c : s) c = static_cast<char>(rng.below(256));
      if (rng.coin()) {
        b.append(s);
      } else {
        char* p = b.prepare(s.size() + rng.below(50));
        std::memcpy(p, s.data(), s.size());
        b.commit(s.size());
      }
      model += s;
    } else {
      size_t n = rng.below(model.size() + 1);
      b.consume(n);
      model.erase(0, n);
    }
    ASSERT_EQ(b.view(), model) << "after operation " << i;
  }
}

// The pitfall: erase-from-front on every consume moves the whole rest each time. A
// server consuming one 30-byte request at a time from a 1 MB pipelined batch would copy
// ~17 GB. Here: a queue that always holds ~1 MB while bytes stream through it.
TEST(Buffer, ConsumingFromTheFrontIsAmortised) {
  Buffer b;
  std::string chunk(32, 'x');
  size_t appended = 0;
  for (int i = 0; i < (1 << 15); ++i) {
    b.append(chunk);
    appended += chunk.size();
  }
  for (int i = 0; i < (1 << 20); ++i) {
    b.consume(32);
    b.append(chunk);
    appended += chunk.size();
  }
  EXPECT_EQ(b.size(), size_t{1} << 20);
  EXPECT_LE(b.bytes_moved(), appended) << "compaction copied more than was ever written";
  EXPECT_LE(b.capacity(), size_t{4} << 20) << "the steady-state queue keeps growing";
}

// Consuming everything should make the next append free: no stale offset to compact.
TEST(Buffer, EmptyingRewinds) {
  Buffer b;
  b.append(std::string(1000, 'a'));
  size_t cap = b.capacity();
  for (int i = 0; i < 10000; ++i) {
    b.consume(b.size());
    b.append(std::string(1000, 'a'));
  }
  EXPECT_EQ(b.capacity(), cap);
  EXPECT_EQ(b.bytes_moved(), 0u);
}

}  // namespace
}  // namespace rd
