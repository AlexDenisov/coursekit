#include "ds/dict/dict.h"

#include <gtest/gtest.h>

#include <map>
#include <set>
#include <string>
#include <unordered_map>

#include "common/rng.h"

namespace rd {
namespace {

std::string key(int i) { return "key:" + std::to_string(i); }

TEST(Dict, Empty) {
  Dict<int> d;
  EXPECT_EQ(d.size(), 0u);
  EXPECT_EQ(d.find("x"), nullptr);
  EXPECT_FALSE(d.erase("x"));
  EXPECT_EQ(d.bucket_count(0), 0u);
  Rng rng(1);
  EXPECT_EQ(d.random_key(rng), nullptr);
}

TEST(Dict, InsertFindErase) {
  Dict<int> d;
  auto [v, added] = d.insert("a", 1);
  ASSERT_NE(v, nullptr);
  EXPECT_TRUE(added);
  EXPECT_EQ(*v, 1);
  auto [v2, added2] = d.insert("a", 2);
  EXPECT_FALSE(added2);
  EXPECT_EQ(*v2, 1) << "insert must not overwrite";
  EXPECT_EQ(*d.find("a"), 1);
  d.get_or_insert("b") = 5;
  EXPECT_EQ(*d.find("b"), 5);
  EXPECT_EQ(d.get_or_insert("b"), 5);
  EXPECT_EQ(d.size(), 2u);
  EXPECT_TRUE(d.erase("a"));
  EXPECT_EQ(d.find("a"), nullptr);
  EXPECT_EQ(d.size(), 1u);
}

TEST(Dict, BinaryKeys) {
  Dict<int> d;
  d.insert(std::string("a\0b", 3), 1);
  d.insert("a", 2);
  EXPECT_EQ(*d.find(std::string_view("a\0b", 3)), 1);
  EXPECT_EQ(*d.find("a"), 2);
}

TEST(Dict, FirstInsertAllocatesFourBuckets) {
  Dict<int> d;
  d.insert("a", 1);
  EXPECT_EQ(d.bucket_count(0), 4u);
  EXPECT_FALSE(d.is_rehashing());
}

// Load factor 1: the 5th key into 4 buckets starts a rehash into 8.
TEST(Dict, GrowsWhenFull) {
  Dict<int> d;
  for (int i = 0; i < 4; ++i) d.insert(key(i), i);
  EXPECT_EQ(d.bucket_count(0), 4u);
  EXPECT_FALSE(d.is_rehashing());
  d.insert(key(4), 4);
  EXPECT_TRUE(d.is_rehashing());
  EXPECT_EQ(d.bucket_count(0), 4u);
  EXPECT_EQ(d.bucket_count(1), 8u);
  while (d.rehash_step(1)) {
  }
  EXPECT_FALSE(d.is_rehashing());
  EXPECT_EQ(d.bucket_count(0), 8u);
  EXPECT_EQ(d.bucket_count(1), 0u);
  for (int i = 0; i < 5; ++i) EXPECT_EQ(*d.find(key(i)), i);
}

// The point of dict.c: growing a table of 65536 entries must not move them all inside
// one command. Each operation moves about one bucket.
TEST(Dict, RehashingIsIncremental) {
  Dict<int> d;
  int n = 65536;
  for (int i = 0; i < n; ++i) d.insert(key(i), i);
  while (d.rehash_step(1000)) {
  }
  ASSERT_EQ(d.bucket_count(0), 65536u);
  d.insert(key(n), n);  // the 65537th key: 65536 entries in 65536 buckets -> grow
  ASSERT_TRUE(d.is_rehashing());
  EXPECT_EQ(d.bucket_count(1), 131072u);
  for (int k = 0; k < 100; ++k) d.find(key(k));
  EXPECT_TRUE(d.is_rehashing()) << "100 lookups finished rehashing 65536 buckets: not incremental";
  for (int k = 0; k <= n; ++k) ASSERT_NE(d.find(key(k)), nullptr) << "lost " << key(k) << " mid-rehash";
}

TEST(Dict, LookupsDuringRehashSeeBothTables) {
  Dict<int> d;
  for (int i = 0; i < 16; ++i) d.insert(key(i), i);
  d.insert(key(16), 16);  // starts 16 -> 32
  ASSERT_TRUE(d.is_rehashing());
  const Dict<int>& cd = d;  // const lookups do no rehash steps
  for (int i = 0; i <= 16; ++i) ASSERT_NE(cd.find(key(i)), nullptr) << key(i);
  EXPECT_TRUE(d.erase(key(3)));
  EXPECT_EQ(cd.find(key(3)), nullptr);
}

TEST(Dict, ShrinksWhenSparse) {
  Dict<int> d;
  for (int i = 0; i < 1000; ++i) d.insert(key(i), i);
  while (d.rehash_step(100)) {
  }
  EXPECT_EQ(d.bucket_count(0), 1024u);
  for (int i = 0; i < 990; ++i) d.erase(key(i));
  while (d.rehash_step(100)) {
  }
  EXPECT_EQ(d.size(), 10u);
  EXPECT_EQ(d.bucket_count(0), 16u) << "10 keys: the table should have shrunk to 16 buckets";
  for (int i = 990; i < 1000; ++i) EXPECT_EQ(*d.find(key(i)), i);
}

TEST(Dict, NeverShrinksBelowFour) {
  Dict<int> d;
  for (int i = 0; i < 100; ++i) d.insert(key(i), i);
  for (int i = 0; i < 100; ++i) d.erase(key(i));
  while (d.rehash_step(100)) {
  }
  EXPECT_EQ(d.size(), 0u);
  EXPECT_EQ(d.bucket_count(0), 4u);
}

TEST(Dict, Clear) {
  Dict<int> d;
  for (int i = 0; i < 100; ++i) d.insert(key(i), i);
  d.clear();
  EXPECT_EQ(d.size(), 0u);
  EXPECT_EQ(d.bucket_count(0), 0u);
  EXPECT_FALSE(d.is_rehashing());
  d.insert("again", 1);
  EXPECT_EQ(*d.find("again"), 1);
}

TEST(Dict, ForEachVisitsEveryEntryOnce) {
  Dict<int> d;
  for (int i = 0; i < 300; ++i) d.insert(key(i), i);  // probably mid-rehash now
  std::map<std::string, int> seen;
  d.for_each([&](const std::string& k, int& v) {
    ++seen[k];
    v += 1000;
  });
  ASSERT_EQ(seen.size(), 300u);
  for (auto& [k, n] : seen) EXPECT_EQ(n, 1) << k;
  EXPECT_EQ(*d.find(key(7)), 1007);
}

// A terrible hash: everything in one chain. Slow, but still correct.
TEST(Dict, AllKeysCollide) {
  Dict<int> d([](std::string_view) -> uint64_t { return 42; });
  for (int i = 0; i < 200; ++i) d.insert(key(i), i);
  for (int i = 0; i < 200; i += 2) EXPECT_TRUE(d.erase(key(i)));
  for (int i = 0; i < 200; ++i) {
    if (i % 2) {
      ASSERT_NE(d.find(key(i)), nullptr);
      EXPECT_EQ(*d.find(key(i)), i);
    } else {
      EXPECT_EQ(d.find(key(i)), nullptr);
    }
  }
}

TEST(Dict, RandomKeyComesFromTheDict) {
  Dict<int> d;
  for (int i = 0; i < 20; ++i) d.insert(key(i), i);
  Rng rng(5);
  std::set<std::string> seen;
  for (int i = 0; i < 2000; ++i) {
    const std::string* k = d.random_key(rng);
    ASSERT_NE(k, nullptr);
    ASSERT_NE(d.find(*k), nullptr);
    seen.insert(*k);
  }
  EXPECT_EQ(seen.size(), 20u) << "some keys never come up";
}

TEST(Dict, ValuesCanBeMoveOnly) {
  Dict<std::unique_ptr<int>> d;
  d.insert("a", std::make_unique<int>(7));
  for (int i = 0; i < 100; ++i) d.insert(key(i), std::make_unique<int>(i));
  EXPECT_EQ(**d.find("a"), 7);
}

// The oracle: std::unordered_map, under random inserts, erases and lookups.
TEST(Dict, MatchesAModel) {
  Rng rng(77);
  Dict<int> d;
  std::unordered_map<std::string, int> model;
  for (int step = 0; step < 200000; ++step) {
    std::string k = key(static_cast<int>(rng.below(rng.coin(0.5) ? 50 : 5000)));
    double r = rng.uniform();
    if (r < 0.4) {
      int v = static_cast<int>(rng.below(1000));
      bool added = d.insert(k, v).second;
      ASSERT_EQ(added, model.emplace(k, v).second) << k;
    } else if (r < 0.7) {
      ASSERT_EQ(d.erase(k), model.erase(k) > 0) << k;
    } else {
      int* v = d.find(k);
      auto it = model.find(k);
      ASSERT_EQ(v != nullptr, it != model.end()) << k << " at step " << step;
      if (v) ASSERT_EQ(*v, it->second);
    }
    ASSERT_EQ(d.size(), model.size());
  }
  size_t count = 0;
  d.for_each([&](const std::string& k, int& v) {
    ++count;
    ASSERT_EQ(model.at(k), v);
  });
  EXPECT_EQ(count, model.size());
}

}  // namespace
}  // namespace rd
