#include <gtest/gtest.h>

#include "persist/rdb_reader/fixture.h"
#include "persist/rdb_reader/rdb_reader.h"

// testdata/redis8_lzf.rdb: the same dataset saved with rdbcompression yes (Redis's
// default), so every string longer than 20 bytes that compresses is LZF-compressed.

namespace rd::rdb {
namespace {

TEST(Lzf, LiteralsAndBackReferences) {
  // "abcabcabc": a literal "abc", then copy 6 bytes from 3 back (overlapping).
  std::string in = std::string("\002abc", 4) + std::string("\200\002", 2);
  EXPECT_EQ(lzf_decompress(in, 9), "abcabcabc");
  EXPECT_EQ(lzf_decompress(in, 8), std::nullopt) << "wrong length";
  EXPECT_EQ(lzf_decompress(std::string("\200\000", 2), 2), std::nullopt) << "reference before the start";
}

TEST(RdbReader, LoadsACompressedDump) {
  FakeClock clock(1'700'000'000'000);
  Keyspace ks(clock);
  LoadResult r = load_snapshot_file("persist/rdb_reader/testdata/redis8_lzf.rdb", ks);
  ASSERT_TRUE(r.ok) << r.error;
  testing::expect_dataset(ks, /*include_soon=*/true);
}

}  // namespace
}  // namespace rd::rdb
