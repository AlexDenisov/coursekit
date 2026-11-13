#include "msg/pubsub/pubsub.h"

#include <gtest/gtest.h>

#include <string>
#include <vector>

// Every expected byte was recorded from Redis 8.10 with several connections
// (`python3 tools/messaging_reference.py`, "pubsub" scenarios).

namespace rd {
namespace {

using Args = std::vector<std::string>;

// A connection that records what was pushed to it.
struct FakeSession : Session {
  void push(std::string_view b) override { pushed += b; }
  std::string take() { return std::exchange(pushed, ""); }
  std::string pushed;
};

class PubSubTest : public ::testing::Test {
 protected:
  PubSubTest() { register_pubsub_commands(table, ps); }
  std::string run(FakeSession& s, Args args) {
    std::string reply;
    if (!pubsub_gate(ps, s, args, reply)) table.dispatch(args, reply, &s);
    return reply;
  }
  PubSub ps;
  CommandTable table;
  FakeSession a, b, c;
};

TEST_F(PubSubTest, SubscribeAndPublish) {
  EXPECT_EQ(run(a, {"SUBSCRIBE", "news", "sports"}),
            "*3\r\n$9\r\nsubscribe\r\n$4\r\nnews\r\n:1\r\n*3\r\n$9\r\nsubscribe\r\n$6\r\nsports\r\n:2\r\n");
  EXPECT_EQ(run(b, {"PUBLISH", "news", "hello"}), ":1\r\n");
  EXPECT_EQ(a.take(), "*3\r\n$7\r\nmessage\r\n$4\r\nnews\r\n$5\r\nhello\r\n");
  EXPECT_EQ(run(b, {"PUBLISH", "weather", "sunny"}), ":0\r\n");
  EXPECT_EQ(a.take(), "");
}

TEST_F(PubSubTest, SubscriberMode) {
  run(a, {"SUBSCRIBE", "news", "sports"});
  EXPECT_EQ(run(a, {"PING"}), "*2\r\n$4\r\npong\r\n$0\r\n\r\n");
  EXPECT_EQ(run(a, {"PING", "hi"}), "*2\r\n$4\r\npong\r\n$2\r\nhi\r\n");
  EXPECT_EQ(run(a, {"GET", "k"}),
            "-ERR Can't execute 'get': only (P|S)SUBSCRIBE / (P|S)UNSUBSCRIBE / PING / QUIT / RESET are allowed in "
            "this context\r\n");
  EXPECT_EQ(run(a, {"UNSUBSCRIBE", "news"}), "*3\r\n$11\r\nunsubscribe\r\n$4\r\nnews\r\n:1\r\n");
  EXPECT_EQ(run(a, {"UNSUBSCRIBE"}), "*3\r\n$11\r\nunsubscribe\r\n$6\r\nsports\r\n:0\r\n");
  EXPECT_EQ(run(a, {"UNSUBSCRIBE"}), "*3\r\n$11\r\nunsubscribe\r\n$-1\r\n:0\r\n");
  std::string reply;
  EXPECT_FALSE(pubsub_gate(ps, a, Args{"GET", "k"}, reply)) << "out of subscriber mode";
}

TEST_F(PubSubTest, Patterns) {
  EXPECT_EQ(run(a, {"PSUBSCRIBE", "news.*"}), "*3\r\n$10\r\npsubscribe\r\n$6\r\nnews.*\r\n:1\r\n");
  run(c, {"SUBSCRIBE", "news.tech"});
  EXPECT_EQ(run(b, {"PUBLISH", "news.tech", "fast"}), ":2\r\n");
  EXPECT_EQ(a.take(), "*4\r\n$8\r\npmessage\r\n$6\r\nnews.*\r\n$9\r\nnews.tech\r\n$4\r\nfast\r\n");
  EXPECT_EQ(c.take(), "*3\r\n$7\r\nmessage\r\n$9\r\nnews.tech\r\n$4\r\nfast\r\n");
  EXPECT_EQ(run(a, {"SUBSCRIBE", "news.tech"}), "*3\r\n$9\r\nsubscribe\r\n$9\r\nnews.tech\r\n:2\r\n");
  // Subscribed both ways: two copies, the channel one first.
  EXPECT_EQ(run(b, {"PUBLISH", "news.tech", "twice"}), ":3\r\n");
  EXPECT_EQ(a.take(),
            "*3\r\n$7\r\nmessage\r\n$9\r\nnews.tech\r\n$5\r\ntwice\r\n*4\r\n$8\r\npmessage\r\n$6\r\nnews.*\r\n$9\r\nnews."
            "tech\r\n$5\r\ntwice\r\n");
  EXPECT_EQ(run(a, {"PUNSUBSCRIBE", "news.*"}), "*3\r\n$12\r\npunsubscribe\r\n$6\r\nnews.*\r\n:1\r\n");
  EXPECT_EQ(run(a, {"PUNSUBSCRIBE"}), "*3\r\n$12\r\npunsubscribe\r\n$-1\r\n:1\r\n");
}

TEST_F(PubSubTest, Introspection) {
  run(a, {"SUBSCRIBE", "news.tech"});
  run(c, {"SUBSCRIBE", "news.tech", "other"});
  run(a, {"PSUBSCRIBE", "x*"});
  EXPECT_EQ(run(b, {"PUBSUB", "NUMSUB", "news.tech", "nothing"}),
            "*4\r\n$9\r\nnews.tech\r\n:2\r\n$7\r\nnothing\r\n:0\r\n");
  EXPECT_EQ(run(b, {"PUBSUB", "NUMPAT"}), ":1\r\n");
  EXPECT_EQ(ps.channels(), (std::set<std::string>{"news.tech", "other"}));
  EXPECT_EQ(ps.channels("news.*"), (std::set<std::string>{"news.tech"}));
}

// The pitfall: a client that disconnects without unsubscribing. Pushing to it afterwards
// is a use-after-free in a real server.
TEST_F(PubSubTest, ForgetDropsEverything) {
  {
    FakeSession gone;
    run(gone, {"SUBSCRIBE", "ch"});
    run(gone, {"PSUBSCRIBE", "c*"});
    ps.forget(gone);
  }
  EXPECT_EQ(run(b, {"PUBLISH", "ch", "x"}), ":0\r\n");
  EXPECT_EQ(ps.numsub("ch"), 0u);
  EXPECT_EQ(ps.numpat(), 0u);
  EXPECT_TRUE(ps.channels().empty()) << "empty channels must disappear";
}

TEST_F(PubSubTest, ManySubscribers) {
  std::vector<FakeSession> subs(1000);
  for (auto& s : subs) run(s, {"SUBSCRIBE", "broadcast"});
  EXPECT_EQ(run(b, {"PUBLISH", "broadcast", "hi"}), ":1000\r\n");
  for (auto& s : subs) ASSERT_EQ(s.take(), "*3\r\n$7\r\nmessage\r\n$9\r\nbroadcast\r\n$2\r\nhi\r\n");
}

TEST_F(PubSubTest, NeedsASession) {
  std::string reply;
  table.dispatch(Args{"SUBSCRIBE", "x"}, reply);
  EXPECT_EQ(reply, "-ERR this command needs a connection\r\n");
}

}  // namespace
}  // namespace rd
