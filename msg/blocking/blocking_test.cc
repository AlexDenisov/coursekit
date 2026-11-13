#include "msg/blocking/blocking.h"

#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "db/lists/lists.h"

// Expected replies recorded from Redis 8.10 with several connections
// (`python3 tools/messaging_reference.py`, "blocking" scenarios).

namespace rd {
namespace {

using Args = std::vector<std::string>;

struct FakeSession : Session {
  void push(std::string_view b) override { pushed += b; }
  std::string take() { return std::exchange(pushed, ""); }
  std::string pushed;
  int blocks = 0, unblocks = 0;
};

class BlockingTest : public ::testing::Test {
 protected:
  BlockingTest() {
    register_keyspace_commands(table, ks);
    register_list_commands(table, ks);
    register_blocking_commands(table, blocking, [this](Session*) { return may_block; });
  }
  // A request from s, then serve_ready(), as the server will do after every command.
  std::string run(FakeSession& s, Args args) {
    std::string reply;
    table.dispatch(args, reply, &s);
    blocking.serve_ready();
    return reply;
  }
  FakeClock clock{1'000'000};
  Keyspace ks{clock};
  Timers timers;
  Blocking blocking{ks, timers, clock, [](Session& s) { ++static_cast<FakeSession&>(s).blocks; },
                    [](Session& s) { ++static_cast<FakeSession&>(s).unblocks; }};
  CommandTable table;
  bool may_block = true;
  FakeSession a, b, c;
};

TEST_F(BlockingTest, PopsAtOnceWhenThereIsSomething) {
  run(b, {"RPUSH", "q2", "x", "y"});
  EXPECT_EQ(run(a, {"BLPOP", "q1", "q2", "0"}), "*2\r\n$2\r\nq2\r\n$1\r\nx\r\n");
  EXPECT_EQ(run(a, {"BRPOP", "q2", "0"}), "*2\r\n$2\r\nq2\r\n$1\r\ny\r\n");
  EXPECT_EQ(a.blocks, 0);
  EXPECT_EQ(ks.find("q2"), nullptr) << "emptied by the pop: deleted";
}

TEST_F(BlockingTest, BlocksThenIsServedByAPush) {
  EXPECT_EQ(run(a, {"BLPOP", "q1", "q2", "0"}), "") << "a blocked client gets no reply yet";
  EXPECT_EQ(a.blocks, 1);
  EXPECT_TRUE(blocking.blocked(a));
  EXPECT_EQ(run(b, {"RPUSH", "q2", "x", "y"}), ":2\r\n");
  EXPECT_EQ(a.take(), "*2\r\n$2\r\nq2\r\n$1\r\nx\r\n");
  EXPECT_EQ(a.unblocks, 1);
  EXPECT_FALSE(blocking.blocked(a));
  EXPECT_EQ(run(b, {"LLEN", "q2"}), ":1\r\n");
}

// Redis: RPUSH q 1 2 3 replies :3 (the length right after the push), and only then are
// the blocked clients served, longest-waiting first.
TEST_F(BlockingTest, ServedAfterThePushInArrivalOrder) {
  run(a, {"BLPOP", "q", "0"});
  run(c, {"BLPOP", "q", "0"});
  EXPECT_EQ(run(b, {"RPUSH", "q", "1", "2", "3"}), ":3\r\n");
  EXPECT_EQ(a.take(), "*2\r\n$1\r\nq\r\n$1\r\n1\r\n");
  EXPECT_EQ(c.take(), "*2\r\n$1\r\nq\r\n$1\r\n2\r\n");
  EXPECT_EQ(run(b, {"LRANGE", "q", "0", "-1"}), "*1\r\n$1\r\n3\r\n");
}

TEST_F(BlockingTest, OnePushServesOneWaiterPerElement) {
  run(a, {"BLPOP", "q", "0"});
  run(c, {"BLPOP", "q", "0"});
  run(b, {"RPUSH", "q", "only"});
  EXPECT_EQ(a.take(), "*2\r\n$1\r\nq\r\n$4\r\nonly\r\n");
  EXPECT_EQ(c.take(), "");
  EXPECT_TRUE(blocking.blocked(c));
  EXPECT_EQ(ks.find("q"), nullptr);
}

// A client waiting on two keys is served once, and leaves the other key's queue.
TEST_F(BlockingTest, ServedOnceAcrossKeys) {
  run(a, {"BRPOP", "k1", "k2", "0"});
  run(b, {"RPUSH", "k1", "x"});
  EXPECT_EQ(a.take(), "*2\r\n$2\r\nk1\r\n$1\r\nx\r\n");
  run(b, {"RPUSH", "k2", "y"});
  EXPECT_EQ(a.take(), "") << "already served";
  EXPECT_EQ(run(b, {"LLEN", "k2"}), ":1\r\n");
  EXPECT_EQ(blocking.blocked_count(), 0u);
}

TEST_F(BlockingTest, Timeout) {
  run(a, {"BRPOP", "q1", "0.1"});
  timers.run_due(clock.now_ms() + 99);
  EXPECT_EQ(a.take(), "");
  timers.run_due(clock.now_ms() + 100);
  EXPECT_EQ(a.take(), "*-1\r\n");
  EXPECT_EQ(a.unblocks, 1);
  run(b, {"RPUSH", "q1", "late"});
  EXPECT_EQ(a.take(), "") << "a timed-out client isn't waiting any more";
}

TEST_F(BlockingTest, TimeoutErrors) {
  EXPECT_EQ(run(a, {"BLPOP", "q1", "abc"}), "-ERR timeout is not a float or out of range\r\n");
  EXPECT_EQ(run(a, {"BLPOP", "q1", "-1"}), "-ERR timeout is negative\r\n");
  EXPECT_EQ(run(a, {"BLPOP", "q1"}), "-ERR wrong number of arguments for 'blpop' command\r\n");
  EXPECT_EQ(a.blocks, 0);
}

// Redis: BLPOP inside MULTI/EXEC never blocks: it replies nil at once.
TEST_F(BlockingTest, NoBlockingWhenNotAllowed) {
  may_block = false;
  EXPECT_EQ(run(a, {"BLPOP", "empty", "0"}), "*-1\r\n");
  EXPECT_EQ(a.blocks, 0);
}

TEST_F(BlockingTest, WrongType) {
  run(b, {"SET", "s", "v"});
  EXPECT_EQ(run(a, {"BLPOP", "s", "0"}), "-WRONGTYPE Operation against a key holding the wrong kind of value\r\n");
}

TEST_F(BlockingTest, ForgetDropsTheWait) {
  {
    FakeSession gone;
    run(gone, {"BLPOP", "q", "5"});
    blocking.forget(gone);
  }
  run(b, {"RPUSH", "q", "x"});
  EXPECT_EQ(run(b, {"LLEN", "q"}), ":1\r\n") << "nobody should have popped for a forgotten client";
  timers.run_due(clock.now_ms() + 10'000);  // its timer must be gone too
}

}  // namespace
}  // namespace rd
