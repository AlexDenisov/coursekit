#include "ds/dict_scan/dict_scan.h"

#include <gtest/gtest.h>

#include <map>
#include <set>
#include <string>
#include <vector>

#include "common/rng.h"

namespace rd {
namespace {

std::string key(int i) { return "k" + std::to_string(i); }

TEST(ScanCursor, ReverseBinaryOrder) {
  std::vector<uint64_t> seq;
  uint64_t c = 0;
  do {
    seq.push_back(c);
    c = scan_next_cursor(c, 7);
  } while (c != 0 && seq.size() < 20);
  EXPECT_EQ(seq, (std::vector<uint64_t>{0, 4, 2, 6, 1, 5, 3, 7}));
}

TEST(ScanCursor, VisitsEveryBucketOnce) {
  for (uint64_t mask : {0ull, 3ull, 15ull, 1023ull}) {
    std::set<uint64_t> seen;
    uint64_t c = 0;
    do {
      EXPECT_TRUE(seen.insert(c).second) << "bucket " << c << " twice, mask " << mask;
      c = scan_next_cursor(c, mask);
    } while (c != 0);
    EXPECT_EQ(seen.size(), mask + 1);
  }
}

// Scans a whole dict, calling `between` after every step. Returns how often each key came
// back.
template <class Between>
std::map<std::string, int> full_scan(Dict<int>& d, Between between) {
  std::map<std::string, int> seen;
  uint64_t cursor = 0;
  int steps = 0;
  do {
    cursor = dict_scan(d, cursor, [&](const std::string& k, const int&) { ++seen[k]; });
    between();
    if (++steps > 1'000'000) {
      ADD_FAILURE() << "the scan never returned to cursor 0";
      break;
    }
  } while (cursor != 0);
  return seen;
}

TEST(DictScan, EmptyDict) {
  Dict<int> d;
  int calls = 0;
  EXPECT_EQ(dict_scan(d, 0, [&](const std::string&, const int&) { ++calls; }), 0u);
  EXPECT_EQ(calls, 0);
}

TEST(DictScan, StaticDictReturnsEveryKeyExactlyOnce) {
  Dict<int> d;
  for (int i = 0; i < 1000; ++i) d.insert(key(i), i);
  while (d.rehash_step(100)) {
  }
  auto seen = full_scan(d, [] {});
  ASSERT_EQ(seen.size(), 1000u);
  for (auto& [k, n] : seen) EXPECT_EQ(n, 1) << k;
}

TEST(DictScan, MidRehashStillReturnsEverything) {
  Dict<int> d;
  for (int i = 0; i < 512; ++i) d.insert(key(i), i);
  while (d.rehash_step(100)) {
  }
  d.insert(key(512), 512);  // starts 512 -> 1024
  ASSERT_TRUE(d.is_rehashing());
  d.rehash_step(100);
  ASSERT_TRUE(d.is_rehashing());
  auto seen = full_scan(d, [] {});
  EXPECT_EQ(seen.size(), 513u);
}

// The guarantee: the table grows (several doublings, with rehashes in progress) between
// calls; every original key still comes back.
TEST(DictScan, SurvivesGrowthBetweenCalls) {
  Dict<int> d;
  for (int i = 0; i < 100; ++i) d.insert(key(i), i);
  int next = 100;
  // (Bounded: a table that grows faster than the scan advances is never finished.)
  auto seen = full_scan(d, [&] {
    for (int j = 0; j < 20 && next < 5000; ++j, ++next) d.insert(key(next), next);
  });
  for (int i = 0; i < 100; ++i) EXPECT_TRUE(seen.count(key(i))) << key(i) << " was never returned";
  EXPECT_GE(d.bucket_count(0) + d.bucket_count(1), 4096u) << "the test meant to grow the table a lot";
}

// ...and shrinks. This is where a naive "increment the cursor" loses keys.
TEST(DictScan, SurvivesShrinkingBetweenCalls) {
  Dict<int> d;
  for (int i = 0; i < 4000; ++i) d.insert(key(i), i);
  while (d.rehash_step(100)) {
  }
  std::vector<int> doomed;
  for (int i = 100; i < 4000; ++i) doomed.push_back(i);
  auto seen = full_scan(d, [&] {
    for (int j = 0; j < 60 && !doomed.empty(); ++j) {
      d.erase(key(doomed.back()));
      doomed.pop_back();
    }
  });
  for (int i = 0; i < 100; ++i) EXPECT_TRUE(seen.count(key(i))) << key(i) << " was never returned";
}

TEST(DictScan, RandomChangesBetweenCalls) {
  Rng rng(11);
  for (int round = 0; round < 20; ++round) {
    Dict<int> d;
    int stable = static_cast<int>(rng.below(300));
    for (int i = 0; i < stable; ++i) d.insert(key(i), i);
    int next = 100000;
    std::vector<int> extra;
    auto seen = full_scan(d, [&] {
      int n = static_cast<int>(rng.below(40));
      for (int j = 0; j < n; ++j) {
        if (rng.coin() || extra.empty()) {
          d.insert(key(next), next);
          extra.push_back(next++);
        } else {
          size_t at = rng.below(extra.size());
          d.erase(key(extra[at]));
          extra.erase(extra.begin() + static_cast<long>(at));
        }
      }
    });
    for (int i = 0; i < stable; ++i) ASSERT_TRUE(seen.count(key(i))) << key(i) << ", round " << round;
  }
}

}  // namespace
}  // namespace rd
