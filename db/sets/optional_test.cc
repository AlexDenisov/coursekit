#include <gtest/gtest.h>

#include <map>
#include <set>
#include <string>
#include <vector>

#include "common/transcript.h"
#include "db/sets/sets.h"

namespace rd {
namespace {

using testing::play;

class RandomSets : public ::testing::Test {
 protected:
  RandomSets() {
    register_keyspace_commands(table, ks);
    register_set_commands(table, ks);
    register_set_optional_commands(table, ks, rng);
  }
  std::vector<std::string> call(std::vector<std::string> args) {
    std::string reply;
    table.dispatch(args, reply);
    std::vector<std::string> items;
    if (!testing::split_array(reply, &items)) {
      if (reply.rfind("$", 0) == 0 && reply != "$-1\r\n") items.push_back(reply.substr(reply.find("\r\n") + 2, reply.size() - reply.find("\r\n") - 4));
    }
    return items;
  }
  FakeClock clock;
  Keyspace ks{clock};
  Rng rng{8};
  CommandTable table;
};

// Recorded from Redis (tools/cases/sets.txt, last group).
TEST_F(RandomSets, DeterministicCases) {
  play(table, {
      {{"SPOP", "missing"}, "$-1\r\n"},
      {{"SPOP", "missing", "2"}, "*0\r\n"},
      {{"SRANDMEMBER", "missing"}, "$-1\r\n"},
      {{"SRANDMEMBER", "missing", "3"}, "*0\r\n"},
      {{"SRANDMEMBER", "missing", "-3"}, "*0\r\n"},
      {{"SADD", "s", "a"}, ":1\r\n"},
      {{"SPOP", "s", "-1"}, "-ERR value is out of range, must be positive\r\n"},
      {{"SRANDMEMBER", "s", "abc"}, "-ERR value is not an integer or out of range\r\n"},
      {{"SPOP", "s", "0"}, "*0\r\n"},
      {{"SRANDMEMBER", "s", "0"}, "*0\r\n"},
      {{"SRANDMEMBER", "s", "5"}, "*1\r\n$1\r\na\r\n"},
      {{"SRANDMEMBER", "s", "-3"}, "*3\r\n$1\r\na\r\n$1\r\na\r\n$1\r\na\r\n"},
      {{"SPOP", "s", "5"}, "*1\r\n$1\r\na\r\n"},
      {{"EXISTS", "s"}, ":0\r\n"},
  });
}

TEST_F(RandomSets, SrandmemberPositiveCountIsDistinct) {
  call({"SADD", "s", "a", "b", "c", "d", "e"});
  for (int i = 0; i < 100; ++i) {
    auto got = call({"SRANDMEMBER", "s", "3"});
    ASSERT_EQ(got.size(), 3u);
    EXPECT_EQ(std::set<std::string>(got.begin(), got.end()).size(), 3u) << "repeats";
  }
  EXPECT_EQ(call({"SRANDMEMBER", "s", "10"}).size(), 5u);
}

TEST_F(RandomSets, SrandmemberNegativeCountAllowsRepeats) {
  call({"SADD", "s", "a", "b"});
  auto got = call({"SRANDMEMBER", "s", "-50"});
  ASSERT_EQ(got.size(), 50u);
  std::map<std::string, int> n;
  for (auto& m : got) ++n[m];
  EXPECT_EQ(n.size(), 2u) << "50 draws from 2 members should hit both";
}

TEST_F(RandomSets, SpopRemovesWhatItReturns) {
  call({"SADD", "s", "a", "b", "c", "d", "e"});
  auto popped = call({"SPOP", "s", "2"});
  ASSERT_EQ(popped.size(), 2u);
  std::string reply;
  for (auto& m : popped) {
    reply.clear();
    table.dispatch(std::vector<std::string>{"SISMEMBER", "s", m}, reply);
    EXPECT_EQ(reply, ":0\r\n") << m << " was popped but is still a member";
  }
  reply.clear();
  table.dispatch(std::vector<std::string>{"SCARD", "s"}, reply);
  EXPECT_EQ(reply, ":3\r\n");
  auto one = call({"SPOP", "s"});
  ASSERT_EQ(one.size(), 1u);
}

TEST_F(RandomSets, SrandmemberCoversTheSet) {
  call({"SADD", "s", "a", "b", "c", "d", "e", "f", "g", "h"});
  std::set<std::string> seen;
  for (int i = 0; i < 400; ++i) {
    auto got = call({"SRANDMEMBER", "s"});
    ASSERT_EQ(got.size(), 1u);
    seen.insert(got[0]);
  }
  EXPECT_EQ(seen.size(), 8u);
}

}  // namespace
}  // namespace rd
