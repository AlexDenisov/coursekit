#pragma once

// What tools/rdb_reference.py put into the dump.rdb fixtures (its DATASET), and a check
// that a keyspace holds exactly that.

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "db/hashes/hashes.h"
#include "db/lists/lists.h"
#include "db/sets/sets.h"
#include "db/zsets/zsets.h"

namespace rd::rdb::testing {

inline std::string str_of(Keyspace& ks, const std::string& key) {
  auto* v = ks.entries().find(key);
  if (v == nullptr || v->value->type() != Type::kString) return "<not a string>";
  return static_cast<StringValue*>(v->value.get())->s;
}

inline std::vector<std::string> list_of(Keyspace& ks, const std::string& key) {
  auto* v = ks.entries().find(key);
  if (v == nullptr || v->value->type() != Type::kList) return {"<not a list>"};
  auto& items = static_cast<ListValue*>(v->value.get())->items;
  return {items.begin(), items.end()};
}

inline std::set<std::string> set_of(Keyspace& ks, const std::string& key) {
  auto* v = ks.entries().find(key);
  if (v == nullptr || v->value->type() != Type::kSet) return {"<not a set>"};
  std::set<std::string> out;
  static_cast<SetValue*>(v->value.get())->members.for_each([&](const std::string& m, bool&) { out.insert(m); });
  return out;
}

inline std::map<std::string, std::string> hash_of(Keyspace& ks, const std::string& key) {
  auto* v = ks.entries().find(key);
  if (v == nullptr || v->value->type() != Type::kHash) return {{"<not a hash>", ""}};
  std::map<std::string, std::string> out;
  static_cast<HashValue*>(v->value.get())->fields.for_each([&](const std::string& f, std::string& val) { out[f] = val; });
  return out;
}

inline std::vector<std::pair<std::string, double>> zset_of(Keyspace& ks, const std::string& key) {
  auto* v = ks.entries().find(key);
  if (v == nullptr || v->value->type() != Type::kZSet) return {{"<not a zset>", 0}};
  std::vector<std::pair<std::string, double>> out;
  auto& z = *static_cast<ZSetValue*>(v->value.get());
  for (auto* n = z.order.first(); n; n = n->next()) out.emplace_back(n->member, n->score);
  EXPECT_EQ(z.scores.size(), out.size()) << key << ": the dict and the skiplist disagree";
  return out;
}

// The dataset; `soon` (expiring in 2050) only if include_soon.
inline void expect_dataset(Keyspace& ks, bool include_soon) {
  EXPECT_EQ(ks.size(), include_soon ? 17u : 16u);
  EXPECT_EQ(str_of(ks, "str"), "hello");
  EXPECT_EQ(str_of(ks, "int"), "12345");
  EXPECT_EQ(str_of(ks, "neg"), "-1");
  EXPECT_EQ(str_of(ks, "big"), "1099511627776");
  EXPECT_EQ(str_of(ks, "long"), std::string(1000, 'L'));
  EXPECT_EQ(str_of(ks, "empty"), "");
  EXPECT_EQ(list_of(ks, "list"), (std::vector<std::string>{"a", "b", "c", "1", "2", "3"}));
  auto biglist = list_of(ks, "biglist");
  ASSERT_EQ(biglist.size(), 2000u);
  for (int i = 0; i < 2000; ++i) {
    char buf[16];
    std::snprintf(buf, sizeof buf, "item%04d", i);
    ASSERT_EQ(biglist[static_cast<size_t>(i)], buf);
  }
  EXPECT_EQ(hash_of(ks, "hash"), (std::map<std::string, std::string>{{"f1", "v1"}, {"f2", "2"}}));
  auto bighash = hash_of(ks, "bighash");
  ASSERT_EQ(bighash.size(), 600u);
  EXPECT_EQ(bighash["f599"], "v599");
  EXPECT_EQ(set_of(ks, "intset"), (std::set<std::string>{"-5", "1", "2", "3"}));
  EXPECT_EQ(set_of(ks, "lpset"), (std::set<std::string>{"x", "y", "z"}));
  auto bigset = set_of(ks, "bigset");
  EXPECT_EQ(bigset.size(), 200u);
  EXPECT_TRUE(bigset.count("m199"));
  EXPECT_EQ(zset_of(ks, "zset"), (std::vector<std::pair<std::string, double>>{{"c", -3}, {"a", 1}, {"b", 2.5}}));
  auto bigzset = zset_of(ks, "bigzset");
  ASSERT_EQ(bigzset.size(), 200u);
  EXPECT_EQ(bigzset[199], (std::pair<std::string, double>{"z199", 199.5}));
  EXPECT_EQ(ks.get_expire("forever"), 4102444800000);
  if (include_soon) EXPECT_EQ(ks.get_expire("soon"), 2524608000000);
  EXPECT_EQ(ks.get_expire("str"), std::nullopt);
}

}  // namespace rd::rdb::testing
