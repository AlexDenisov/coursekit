#include "persist/rdb_reader/rdb_reader.h"

#include <gtest/gtest.h>

#include <fstream>
#include <memory>
#include <sstream>
#include <string>

#include "persist/rdb_reader/fixture.h"
#include "persist/rdb_writer/rdb_writer.h"

// testdata/redis8_plain.rdb was written by Redis 8.10 (`python3 tools/rdb_reference.py`),
// with rdbcompression off; its contents are in fixture.h.

namespace rd::rdb {
namespace {

constexpr char kPlain[] = "persist/rdb_reader/testdata/redis8_plain.rdb";

std::string read_file(const char* path) {
  std::ifstream f(path, std::ios::binary);
  std::stringstream ss;
  ss << f.rdbuf();
  return ss.str();
}

TEST(RdbReader, LoadsADumpWrittenByRedis) {
  FakeClock clock(1'700'000'000'000);  // 2023
  Keyspace ks(clock);
  LoadResult r = load_snapshot_file(kPlain, ks);
  ASSERT_TRUE(r.ok) << r.error;
  EXPECT_EQ(r.keys_loaded, 17u);
  EXPECT_EQ(r.expired_skipped, 0u);
  testing::expect_dataset(ks, /*include_soon=*/true);
}

// Loading in 2052: the key that expired in 2050 is skipped.
TEST(RdbReader, SkipsKeysThatHaveExpired) {
  FakeClock clock(2'600'000'000'000);
  Keyspace ks(clock);
  LoadResult r = load_snapshot_file(kPlain, ks);
  ASSERT_TRUE(r.ok) << r.error;
  EXPECT_EQ(r.keys_loaded, 16u);
  EXPECT_EQ(r.expired_skipped, 1u);
  testing::expect_dataset(ks, /*include_soon=*/false);
}

TEST(RdbReader, MissingFile) {
  FakeClock clock;
  Keyspace ks(clock);
  EXPECT_FALSE(load_snapshot_file("/nonexistent/dump.rdb", ks).ok);
}

TEST(RdbReader, RejectsBadMagicAndVersions) {
  FakeClock clock;
  Keyspace ks(clock);
  EXPECT_FALSE(load_snapshot("", ks).ok);
  EXPECT_FALSE(load_snapshot("RESID0011\377", ks).ok);
  EXPECT_FALSE(load_snapshot("REDIS0099\377", ks).ok) << "a version from the future";
}

// The pitfall: a flipped bit in the middle of a big value still parses fine; only the
// checksum notices.
TEST(RdbReader, DetectsCorruptionWithTheChecksum) {
  std::string data = read_file(kPlain);
  size_t at = data.find(std::string(100, 'L'));
  ASSERT_NE(at, std::string::npos);
  data[at + 50] = 'M';
  FakeClock clock;
  Keyspace ks(clock);
  LoadResult r = load_snapshot(data, ks);
  EXPECT_FALSE(r.ok);
  EXPECT_NE(r.error.find("checksum"), std::string::npos) << r.error;
}

TEST(RdbReader, TruncatedFileIsAnErrorNotACrash) {
  std::string data = read_file(kPlain);
  FakeClock clock;
  for (size_t n : {size_t{5}, size_t{9}, size_t{100}, data.size() / 2, data.size() - 9, data.size() - 1}) {
    Keyspace ks(clock);
    EXPECT_FALSE(load_snapshot(std::string_view(data).substr(0, n), ks).ok) << "truncated to " << n;
  }
}

TEST(RdbReader, ZeroChecksumMeansNotComputed) {
  std::string data = read_file(kPlain);
  data.replace(data.size() - 8, 8, std::string(8, '\0'));
  FakeClock clock(1'700'000'000'000);
  Keyspace ks(clock);
  LoadResult r = load_snapshot(data, ks);
  EXPECT_TRUE(r.ok) << r.error;
}

// Your writer and your reader agree.
TEST(RdbReader, RoundTripsTheWriter) {
  FakeClock clock(1'700'000'000'000);
  Keyspace a(clock);
  ASSERT_TRUE(load_snapshot_file(kPlain, a).ok);
  std::string written = write_snapshot(a);
  Keyspace b(clock);
  LoadResult r = load_snapshot(written, b);
  ASSERT_TRUE(r.ok) << r.error;
  testing::expect_dataset(b, /*include_soon=*/true);
}

}  // namespace
}  // namespace rd::rdb
