#include "ds/skiplist/skiplist.h"

#include <gtest/gtest.h>

#include <chrono>
#include <iterator>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "common/rng.h"

namespace rd {
namespace {

using Elem = std::pair<double, std::string>;  // std::set's order is exactly the skiplist's

std::vector<Elem> contents(const SkipList& sl) {
  std::vector<Elem> out;
  for (auto* n = sl.first(); n != nullptr; n = n->next()) out.emplace_back(n->score, n->member);
  return out;
}

// Every invariant the structure relies on, checked from the outside.
void check_invariants(const SkipList& sl) {
  std::vector<const SkipList::Node*> nodes;
  for (auto* n = sl.first(); n != nullptr; n = n->next()) nodes.push_back(n);
  ASSERT_EQ(nodes.size(), sl.size());
  ASSERT_EQ(sl.last(), nodes.empty() ? nullptr : nodes.back());
  for (size_t i = 0; i < nodes.size(); ++i) {
    ASSERT_EQ(nodes[i]->backward, i == 0 ? nullptr : nodes[i - 1]) << "backward of node " << i;
    ASSERT_GE(nodes[i]->levels.size(), 1u);
    ASSERT_LE(static_cast<int>(nodes[i]->levels.size()), sl.level());
    for (size_t l = 0; l < nodes[i]->levels.size(); ++l) {
      const SkipList::Node* f = nodes[i]->levels[l].forward;
      if (f == nullptr) continue;
      // the span is the distance in level-0 steps
      size_t j = i + nodes[i]->levels[l].span;
      ASSERT_LT(j, nodes.size()) << "span past the end at node " << i << " level " << l;
      ASSERT_EQ(nodes[j], f) << "wrong span at node " << i << " level " << l;
    }
  }
}

TEST(SkipList, Empty) {
  SkipList sl;
  EXPECT_EQ(sl.size(), 0u);
  EXPECT_EQ(sl.first(), nullptr);
  EXPECT_EQ(sl.at(0), nullptr);
  EXPECT_EQ(sl.rank(1, "a"), -1);
  EXPECT_FALSE(sl.erase(1, "a"));
}

TEST(SkipList, OrderedByScoreThenMember) {
  SkipList sl;
  sl.insert(2, "b");
  sl.insert(1, "z");
  sl.insert(2, "a");
  sl.insert(-5, "m");
  EXPECT_EQ(contents(sl), (std::vector<Elem>{{-5, "m"}, {1, "z"}, {2, "a"}, {2, "b"}}));
  check_invariants(sl);
}

TEST(SkipList, RankAndAt) {
  SkipList sl;
  for (int i = 0; i < 100; ++i) sl.insert(i * 10, "m" + std::to_string(i));
  for (int i = 0; i < 100; ++i) {
    EXPECT_EQ(sl.rank(i * 10, "m" + std::to_string(i)), i);
    ASSERT_NE(sl.at(static_cast<size_t>(i)), nullptr);
    EXPECT_EQ(sl.at(static_cast<size_t>(i))->score, i * 10);
  }
  EXPECT_EQ(sl.at(100), nullptr);
  EXPECT_EQ(sl.rank(10, "nope"), -1);
}

TEST(SkipList, Ranges) {
  SkipList sl;
  for (int i = 1; i <= 5; ++i) sl.insert(i, "m" + std::to_string(i));
  EXPECT_EQ(sl.first_from(2, false)->score, 2);
  EXPECT_EQ(sl.first_from(2, true)->score, 3);
  EXPECT_EQ(sl.first_from(2.5, false)->score, 3);
  EXPECT_EQ(sl.first_from(6, false), nullptr);
  EXPECT_EQ(sl.last_until(4, false)->score, 4);
  EXPECT_EQ(sl.last_until(4, true)->score, 3);
  EXPECT_EQ(sl.last_until(0.5, false), nullptr);
  EXPECT_EQ(sl.first_from(-1e300, false)->score, 1);
}

TEST(SkipList, UpdateScore) {
  SkipList sl;
  sl.insert(1, "a");
  sl.insert(2, "b");
  sl.insert(3, "c");
  auto* n = sl.update_score(1, "a", 10);
  ASSERT_NE(n, nullptr);
  EXPECT_EQ(n->member, "a");
  EXPECT_EQ(contents(sl), (std::vector<Elem>{{2, "b"}, {3, "c"}, {10, "a"}}));
  check_invariants(sl);
}

// About 1/4 of nodes have height >= 2, 1/16 >= 3, ...
TEST(SkipList, LevelsAreGeometric) {
  SkipList sl(42);
  int counts[5] = {};
  const int n = 40000;
  for (int i = 0; i < n; ++i) {
    int l = sl.random_level();
    ASSERT_GE(l, 1);
    ASSERT_LE(l, SkipList::kMaxLevel);
    for (int k = 0; k < 5 && k < l; ++k) ++counts[k];
  }
  EXPECT_EQ(counts[0], n);
  EXPECT_NEAR(counts[1] / double(n), 0.25, 0.02);
  EXPECT_NEAR(counts[2] / double(n), 0.0625, 0.01);
}

// The oracle: std::set of (score, member). Random inserts, erases and updates; after
// each, the contents, every rank and every invariant must match.
TEST(SkipList, MatchesAModel) {
  Rng rng(9);
  SkipList sl(9);
  std::set<Elem> model;
  for (int step = 0; step < 4000; ++step) {
    double r = rng.uniform();
    if (r < 0.5 || model.empty()) {
      Elem e{static_cast<double>(rng.between(-50, 50)) / 4, "m" + std::to_string(rng.below(300))};
      bool exists = false;
      for (auto& m : model) exists |= m.second == e.second;
      if (exists) continue;  // members are unique (the zset's dict guarantees it)
      sl.insert(e.first, e.second);
      model.insert(e);
    } else {
      auto it = model.begin();
      std::advance(it, static_cast<long>(rng.below(model.size())));
      Elem e = *it;
      model.erase(it);
      if (r < 0.75) {
        ASSERT_TRUE(sl.erase(e.first, e.second));
      } else {
        double ns = static_cast<double>(rng.between(-50, 50)) / 4;
        sl.update_score(e.first, e.second, ns);
        model.insert({ns, e.second});
      }
    }
    if (step % 50 == 0) {
      ASSERT_EQ(contents(sl), std::vector<Elem>(model.begin(), model.end())) << "step " << step;
      check_invariants(sl);
      int64_t i = 0;
      for (auto& e : model) {
        ASSERT_EQ(sl.rank(e.first, e.second), i);
        ASSERT_EQ(sl.at(static_cast<size_t>(i))->member, e.second);
        ++i;
      }
    }
  }
}

// The point of the spans: rank and at() in O(log n). Counting level-0 steps instead would
// be 100 000 * 100 000 / 2 steps here.
TEST(SkipList, RankIsLogarithmic) {
  SkipList sl;
  const int n = 100000;
  for (int i = 0; i < n; ++i) sl.insert(i, "m" + std::to_string(i));
  auto t0 = std::chrono::steady_clock::now();
  int64_t sum = 0;
  for (int i = 0; i < n; ++i) sum += sl.rank(i, "m" + std::to_string(i));
  for (int i = 0; i < n; ++i) sum += static_cast<int64_t>(sl.at(static_cast<size_t>(i))->score);
  auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();
  EXPECT_EQ(sum, int64_t{n} * (n - 1));
  EXPECT_LT(ms, 2000);
}

}  // namespace
}  // namespace rd
