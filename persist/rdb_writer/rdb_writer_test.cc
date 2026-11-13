#include "persist/rdb_writer/rdb_writer.h"

#include <gtest/gtest.h>
#include <unistd.h>

#include <cstdio>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>

#include "db/hashes/hashes.h"
#include "db/lists/lists.h"
#include "db/sets/sets.h"
#include "db/zsets/zsets.h"
#include "persist/rdb_writer/rdb_test_parser.h"

namespace rd::rdb {
namespace {

template <size_t N>
std::string bytes(const char (&lit)[N]) {
  return std::string(lit, N - 1);
}

std::string len(uint64_t n) {
  std::string out;
  write_length(out, n);
  return out;
}

std::string str(std::string_view s) {
  std::string out;
  write_string(out, s);
  return out;
}

TEST(WriteLength, Encodings) {
  EXPECT_EQ(len(0), bytes("\000"));
  EXPECT_EQ(len(63), bytes("\077"));
  EXPECT_EQ(len(64), bytes("\100\100"));
  EXPECT_EQ(len(16383), bytes("\177\377"));
  EXPECT_EQ(len(16384), bytes("\200\000\000\100\000"));
  EXPECT_EQ(len(uint64_t{1} << 32), bytes("\201\000\000\000\001\000\000\000\000"));
}

// Every expected encoding is what Redis 8.10's DUMP of SET k <value> contains.
TEST(WriteString, MatchesRedis) {
  EXPECT_EQ(str("hello"), bytes("\005hello"));
  EXPECT_EQ(str(""), bytes("\000"));
  EXPECT_EQ(str(std::string(64, 'x')), "@@" + std::string(64, 'x'));
  EXPECT_EQ(str(std::string(16384, 'y')).substr(0, 5), bytes("\200\000\000@\000"));
}

TEST(WriteString, IntegersAreStoredAsNumbers) {
  EXPECT_EQ(str("12345"), bytes("\30190"));
  EXPECT_EQ(str("-1"), bytes("\300\377"));
  EXPECT_EQ(str("100"), bytes("\300d"));
  EXPECT_EQ(str("127"), bytes("\300\177"));
  EXPECT_EQ(str("128"), bytes("\301\200\000"));
  EXPECT_EQ(str("-128"), bytes("\300\200"));
  EXPECT_EQ(str("-129"), bytes("\301\177\377"));
  EXPECT_EQ(str("300"), bytes("\301,\001"));
  EXPECT_EQ(str("70000"), bytes("\302p\021\001\000"));
  EXPECT_EQ(str("2147483647"), bytes("\302\377\377\377\177"));
  EXPECT_EQ(str("-2147483648"), bytes("\302\000\000\000\200"));
}

// The pitfall: only strings that read back *identically* may become numbers.
TEST(WriteString, IntegerLookalikesStayStrings) {
  EXPECT_EQ(str("2147483648"), bytes("\n2147483648"));        // doesn't fit int32
  EXPECT_EQ(str("1099511627776"), bytes("\r1099511627776"));  // int64, but Redis only uses 32 bits
  EXPECT_EQ(str("01"), bytes("\00201"));                      // "1" would come back
}

class Snapshot : public ::testing::Test {
 protected:
  void fill() {
    ks.set("str", std::make_unique<StringValue>("hello"));
    ks.set("num", std::make_unique<StringValue>("12345"));
    auto l = std::make_unique<ListValue>();
    for (auto s : {"a", "b", "c"}) l->items.push_back(s);
    ks.set("list", std::move(l));
    auto h = std::make_unique<HashValue>();
    h->fields.insert("f1", "v1");
    h->fields.insert("f2", "2");
    ks.set("hash", std::move(h));
    auto s = std::make_unique<SetValue>();
    for (auto m : {"x", "y"}) s->members.insert(m, true);
    ks.set("set", std::move(s));
    auto z = std::make_unique<ZSetValue>();
    z->set("a", 1.5);
    z->set("b", -2);
    ks.set("zset", std::move(z));
    ks.set("ttl", std::make_unique<StringValue>("v"));
    ks.set_expire("ttl", 4102444800000);
  }
  FakeClock clock{1'700'000'000'000};
  Keyspace ks{clock};
};

TEST_F(Snapshot, EmptyKeyspace) {
  std::string out = write_snapshot(ks);
  auto p = testing::parse(out);
  ASSERT_EQ(p.error, "");
  EXPECT_EQ(p.magic, "REDIS0011");
  EXPECT_TRUE(p.keys.empty());
}

TEST_F(Snapshot, EveryTypeRoundTrips) {
  fill();
  auto p = testing::parse(write_snapshot(ks));
  ASSERT_EQ(p.error, "");
  ASSERT_EQ(p.keys.size(), 7u);
  EXPECT_EQ(p.keys["str"].type, kTypeString);
  EXPECT_EQ(p.keys["str"].items, std::vector<std::string>{"hello"});
  EXPECT_EQ(p.keys["num"].items, std::vector<std::string>{"12345"});
  EXPECT_EQ(p.keys["list"].type, kTypeList);
  EXPECT_EQ(p.keys["list"].items, (std::vector<std::string>{"a", "b", "c"}));
  EXPECT_EQ(p.keys["hash"].type, kTypeHash);
  EXPECT_EQ(p.keys["hash"].items.size(), 4u);
  EXPECT_EQ(p.keys["set"].type, kTypeSet);
  EXPECT_EQ(p.keys["set"].items.size(), 2u);
  EXPECT_EQ(p.keys["zset"].type, kTypeZSet2);
  EXPECT_EQ(p.keys["zset"].items, (std::vector<std::string>{"b", std::to_string(-2.0), "a", std::to_string(1.5)}))
      << "zset members in score order, each followed by its binary score";
  EXPECT_EQ(p.keys["ttl"].expire_ms, 4102444800000);
  EXPECT_EQ(p.keys["str"].expire_ms, std::nullopt);
}

TEST_F(Snapshot, ChecksumCoversEverythingBeforeIt) {
  fill();
  std::string out = write_snapshot(ks);
  auto p = testing::parse(out);
  ASSERT_EQ(p.error, "");
  EXPECT_EQ(p.stored_crc, testing::crc64_reference(std::string_view(out).substr(0, p.crc_offset)));
}

TEST_F(Snapshot, ManyKeysAndBigValues) {
  for (int i = 0; i < 5000; ++i) ks.set("k" + std::to_string(i), std::make_unique<StringValue>(std::string(i % 300, 'v')));
  auto l = std::make_unique<ListValue>();
  for (int i = 0; i < 20000; ++i) l->items.push_back(std::to_string(i));
  ks.set("biglist", std::move(l));
  auto p = testing::parse(write_snapshot(ks));
  ASSERT_EQ(p.error, "");
  EXPECT_EQ(p.keys.size(), 5001u);
  EXPECT_EQ(p.keys["k299"].items[0], std::string(299, 'v'));
  EXPECT_EQ(p.keys["biglist"].items[19999], "19999");
}

TEST_F(Snapshot, SaveIsAtomic) {
  fill();
  char dir[] = "/tmp/rdbtestXXXXXX";
  ASSERT_NE(::mkdtemp(dir), nullptr);
  std::string path = std::string(dir) + "/dump.rdb";
  ASSERT_TRUE(save_snapshot(ks, path));
  std::ifstream f(path, std::ios::binary);
  std::stringstream ss;
  ss << f.rdbuf();
  EXPECT_EQ(ss.str(), write_snapshot(ks));
  EXPECT_NE(::access((path + ".tmp").c_str(), F_OK), 0) << "the temp file should have been renamed away";
  EXPECT_FALSE(save_snapshot(ks, "/nonexistent/dir/dump.rdb"));
  std::remove(path.c_str());
  ::rmdir(dir);
}

}  // namespace
}  // namespace rd::rdb
