#include <gtest/gtest.h>

#include <map>
#include <string>
#include <vector>

#include "common/rng.h"
#include "db/encodings/encodings.h"

// Expected encodings from Redis 8.10 (tools/cases/encodings.txt).

namespace rd {
namespace {

TEST(CompactZSet, ListpackUntil128Members) {
  CompactZSet z;
  for (int i = 0; i < 128; ++i) z.add("m" + std::to_string(i), i);
  EXPECT_EQ(z.encoding(), "listpack");
  z.add("m128", 128);
  EXPECT_EQ(z.encoding(), "skiplist");
  EXPECT_EQ(z.score("m5"), 5);
}

TEST(CompactZSet, LongMemberConverts) {
  CompactZSet z;
  z.add("a", 1);
  EXPECT_EQ(z.encoding(), "listpack");
  z.add(std::string(65, 'z'), 2);
  EXPECT_EQ(z.encoding(), "skiplist");
}

TEST(CompactZSet, SortedInBothEncodings) {
  CompactZSet z;
  z.add("b", 1);
  z.add("a", 1);
  z.add("c", 0.5);
  z.add("b", -1);
  using Items = std::vector<std::pair<std::string, double>>;
  EXPECT_EQ(z.items(), (Items{{"b", -1}, {"c", 0.5}, {"a", 1}}));
  for (int i = 0; i < 200; ++i) z.add("x" + std::to_string(i), 1000 + i);
  ASSERT_EQ(z.encoding(), "skiplist");
  EXPECT_EQ(z.items()[0], (std::pair<std::string, double>{"b", -1}));
}

TEST(CompactZSet, ScoresSurviveTheListpackExactly) {
  CompactZSet z;
  z.add("p", 0.1 + 0.2);
  z.add("q", 1e-300);
  z.add("r", 12345678901234567890.0);
  EXPECT_EQ(z.score("p"), 0.1 + 0.2);
  EXPECT_EQ(z.score("q"), 1e-300);
  EXPECT_EQ(z.score("r"), 12345678901234567890.0);
}

TEST(CompactZSet, MatchesAModel) {
  Rng rng(12);
  CompactZSet z;
  std::map<std::string, double> model;
  for (int i = 0; i < 2000; ++i) {
    std::string m = "m" + std::to_string(rng.below(200));
    if (rng.coin(0.7)) {
      double s = static_cast<double>(rng.between(-100, 100)) / 8;
      ASSERT_EQ(z.add(m, s), model.find(m) == model.end());
      model[m] = s;
    } else {
      ASSERT_EQ(z.remove(m), model.erase(m) > 0);
    }
    ASSERT_EQ(z.size(), model.size());
  }
  for (auto& [m, s] : model) ASSERT_EQ(z.score(m), s);
  auto items = z.items();
  for (size_t i = 1; i < items.size(); ++i) {
    ASSERT_TRUE(items[i - 1].second < items[i].second ||
                (items[i - 1].second == items[i].second && items[i - 1].first < items[i].first));
  }
}

}  // namespace
}  // namespace rd
