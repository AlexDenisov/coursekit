#include "db/encodings/encodings.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "common/rng.h"

// Every expected encoding is what OBJECT ENCODING said in Redis 8.10 after the same
// operations: `python3 tools/redis_reference.py tools/cases/encodings.txt`.

namespace rd {
namespace {

using Pairs = std::vector<std::pair<std::string, std::string>>;

// ---- hashes ----

TEST(CompactHash, StartsAsAListpack) {
  CompactHash h;
  EXPECT_TRUE(h.set("f", "v"));
  EXPECT_FALSE(h.set("f", "w"));
  EXPECT_EQ(h.get("f"), "w");
  EXPECT_EQ(h.get("nope"), std::nullopt);
  EXPECT_EQ(h.encoding(), "listpack");
}

// HSET 512 fields: listpack; the 513th: hashtable; deleting back below: still hashtable.
TEST(CompactHash, ConvertsPastTheEntryLimit) {
  CompactHash h;
  for (int i = 0; i < 512; ++i) h.set("f" + std::to_string(i), "v");
  EXPECT_EQ(h.encoding(), "listpack");
  h.set("f512", "v");
  EXPECT_EQ(h.encoding(), "hashtable");
  h.remove("f512");
  h.remove("f511");
  h.remove("f510");
  EXPECT_EQ(h.encoding(), "hashtable") << "Redis never converts back";
  EXPECT_EQ(h.size(), 510u);
  EXPECT_EQ(h.get("f7"), "v");
}

TEST(CompactHash, ConvertsForALongValueOrField) {
  CompactHash h;
  h.set("a", std::string(64, 'x'));
  EXPECT_EQ(h.encoding(), "listpack");
  h.set("b", std::string(65, 'x'));
  EXPECT_EQ(h.encoding(), "hashtable");
  EXPECT_EQ(h.get("a"), std::string(64, 'x'));
  CompactHash h2;
  h2.set(std::string(65, 'f'), "v");
  EXPECT_EQ(h2.encoding(), "hashtable");
}

// HSET h b 1 a 2 c 3 / HSET h a 20 / HDEL h b / HSET h b 10, with HGETALL after each.
TEST(CompactHash, ListpackKeepsInsertionOrder) {
  CompactHash h;
  h.set("b", "1");
  h.set("a", "2");
  h.set("c", "3");
  EXPECT_EQ(h.items(), (Pairs{{"b", "1"}, {"a", "2"}, {"c", "3"}}));
  h.set("a", "20");
  EXPECT_EQ(h.items(), (Pairs{{"b", "1"}, {"a", "20"}, {"c", "3"}}));
  h.remove("b");
  h.set("b", "10");
  EXPECT_EQ(h.items(), (Pairs{{"a", "20"}, {"c", "3"}, {"b", "10"}}));
}

// The pitfall: a value that happens to equal a field name. Searching the listpack must
// only look at field positions.
TEST(CompactHash, ValuesAreNotFields) {
  CompactHash h;
  h.set("a", "b");
  EXPECT_EQ(h.get("b"), std::nullopt);
  EXPECT_TRUE(h.set("b", "x"));
  EXPECT_EQ(h.get("a"), "b");
  EXPECT_FALSE(h.remove("x"));
  EXPECT_EQ(h.size(), 2u);
}

TEST(CompactHash, MatchesAModelAcrossTheConversion) {
  Rng rng(4);
  CompactHash h;
  std::map<std::string, std::string> model;
  for (int i = 0; i < 3000; ++i) {
    std::string f = "f" + std::to_string(rng.below(700));
    if (rng.coin(0.7)) {
      std::string v = std::to_string(rng.below(1000));
      ASSERT_EQ(h.set(f, v), model.find(f) == model.end());
      model[f] = v;
    } else {
      ASSERT_EQ(h.remove(f), model.erase(f) > 0);
    }
    ASSERT_EQ(h.size(), model.size());
  }
  auto items = h.items();
  std::sort(items.begin(), items.end());
  EXPECT_EQ(items, Pairs(model.begin(), model.end()));
}

// ---- sets ----

TEST(CompactSet, IntegersMakeAnIntset) {
  CompactSet s;
  s.add("1");
  s.add("2");
  s.add("3");
  EXPECT_EQ(s.encoding(), "intset");
  s.add("9223372036854775807");
  EXPECT_EQ(s.encoding(), "intset");
  s.add("abc");
  EXPECT_EQ(s.encoding(), "listpack");
  s.remove("abc");
  EXPECT_EQ(s.encoding(), "listpack");
  EXPECT_TRUE(s.contains("9223372036854775807"));
}

TEST(CompactSet, StringsMakeAListpack) {
  CompactSet s;
  s.add("a");
  s.add("b");
  EXPECT_EQ(s.encoding(), "listpack");
  s.add("1");
  EXPECT_EQ(s.encoding(), "listpack") << "an integer doesn't take a listpack back to intset";
}

TEST(CompactSet, IntsetLimit) {
  CompactSet s;
  for (int i = 0; i < 512; ++i) s.add(std::to_string(i));
  EXPECT_EQ(s.encoding(), "intset");
  s.add("512");
  EXPECT_EQ(s.encoding(), "hashtable");
  EXPECT_EQ(s.size(), 513u);
}

// The subtle one: 128 integers plus a string would be 129 listpack entries, too many, so
// Redis goes straight to hashtable; 127 + 1 fits in a listpack.
TEST(CompactSet, IntsetToListpackOnlyIfItFits) {
  CompactSet big, small;
  for (int i = 0; i < 128; ++i) big.add(std::to_string(i));
  for (int i = 0; i < 127; ++i) small.add(std::to_string(i));
  big.add("x");
  small.add("x");
  EXPECT_EQ(big.encoding(), "hashtable");
  EXPECT_EQ(small.encoding(), "listpack");
  EXPECT_TRUE(small.contains("100"));
  EXPECT_TRUE(small.contains("x"));
}

TEST(CompactSet, ListpackLimits) {
  CompactSet s;
  for (int i = 0; i < 128; ++i) s.add("m" + std::to_string(i));
  EXPECT_EQ(s.encoding(), "listpack");
  s.add("m128");
  EXPECT_EQ(s.encoding(), "hashtable");
  CompactSet t;
  t.add("a");
  t.add(std::string(65, 'y'));
  EXPECT_EQ(t.encoding(), "hashtable");
  CompactSet u;
  u.add("1");
  u.add("2");
  u.add(std::string(65, 'y'));
  EXPECT_EQ(u.encoding(), "hashtable");
}

// SADD s z y x -> z y x (insertion order); SADD t 3 1 2 -> 1 2 3 (an intset is sorted).
TEST(CompactSet, MemberOrder) {
  CompactSet s, t;
  for (auto m : {"z", "y", "x"}) s.add(m);
  for (auto m : {"3", "1", "2"}) t.add(m);
  EXPECT_EQ(s.members(), (std::vector<std::string>{"z", "y", "x"}));
  EXPECT_EQ(t.members(), (std::vector<std::string>{"1", "2", "3"}));
}

// The pitfall: "01" isn't an integer for Redis; it can't go into an intset as 1.
TEST(CompactSet, IntegerLikeStringsAreStrings) {
  CompactSet s;
  s.add("1");
  s.add("01");
  EXPECT_EQ(s.encoding(), "listpack");
  EXPECT_EQ(s.size(), 2u);
  EXPECT_TRUE(s.contains("01"));
  EXPECT_TRUE(s.contains("1"));
}

TEST(CompactSet, MatchesAModelAcrossConversions) {
  Rng rng(6);
  for (int round = 0; round < 10; ++round) {
    CompactSet s;
    std::set<std::string> model;
    for (int i = 0; i < 1500; ++i) {
      std::string m = rng.coin(0.8) ? std::to_string(rng.between(-300, 300)) : "s" + std::to_string(rng.below(200));
      if (rng.coin(0.7)) {
        ASSERT_EQ(s.add(m), model.insert(m).second) << m;
      } else {
        ASSERT_EQ(s.remove(m), model.erase(m) > 0) << m;
      }
      ASSERT_EQ(s.size(), model.size());
    }
    auto members = s.members();
    EXPECT_EQ(std::set<std::string>(members.begin(), members.end()), model);
  }
}

}  // namespace
}  // namespace rd
