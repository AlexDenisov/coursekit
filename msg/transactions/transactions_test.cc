#include "msg/transactions/transactions.h"

#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "db/generic/generic.h"

// Every expected reply recorded from Redis 8.10 with two connections
// (`python3 tools/messaging_reference.py`, "transactions", "transaction errors",
// "watch").

namespace rd {
namespace {

using Args = std::vector<std::string>;

struct FakeSession : Session {
  void push(std::string_view) override {}
};

class Tx : public ::testing::Test {
 protected:
  Tx() {
    register_connection_commands(table);
    register_keyspace_commands(table, ks);
    register_generic_commands(table, ks, rng);
  }
  std::string run(FakeSession& s, Args args) {
    std::string reply;
    if (!tx.handle(table, s, args, reply)) table.dispatch(args, reply, &s);
    return reply;
  }
  FakeClock clock;
  Keyspace ks{clock};
  Rng rng{1};
  CommandTable table;
  Transactions tx{ks};
  FakeSession a, b;
};

TEST_F(Tx, QueueAndExec) {
  EXPECT_EQ(run(a, {"MULTI"}), "+OK\r\n");
  EXPECT_TRUE(tx.in_multi(a));
  EXPECT_EQ(run(a, {"SET", "k", "1"}), "+QUEUED\r\n");
  EXPECT_EQ(run(a, {"INCR", "k"}), "+QUEUED\r\n");
  EXPECT_EQ(run(a, {"GET", "k"}), "+QUEUED\r\n");
  EXPECT_EQ(ks.find("k"), nullptr) << "queued commands must not run yet";
  EXPECT_EQ(run(a, {"EXEC"}), "*3\r\n+OK\r\n:2\r\n$1\r\n2\r\n");
  EXPECT_FALSE(tx.in_multi(a));
  EXPECT_EQ(run(a, {"EXEC"}), "-ERR EXEC without MULTI\r\n");
  EXPECT_EQ(run(a, {"DISCARD"}), "-ERR DISCARD without MULTI\r\n");
  EXPECT_EQ(run(a, {"MULTI"}), "+OK\r\n");
  EXPECT_EQ(run(a, {"MULTI"}), "-ERR MULTI calls can not be nested\r\n");
  EXPECT_EQ(run(a, {"DISCARD"}), "+OK\r\n");
}

TEST_F(Tx, OtherClientsAreUnaffected) {
  run(a, {"MULTI"});
  run(a, {"SET", "k", "a"});
  EXPECT_EQ(run(b, {"SET", "k", "b"}), "+OK\r\n") << "b isn't in a transaction";
  EXPECT_EQ(run(a, {"EXEC"}), "*1\r\n+OK\r\n");
  EXPECT_EQ(run(b, {"GET", "k"}), "$1\r\na\r\n");
}

// Errors caught while queueing (unknown command, wrong arity) abort the whole transaction.
TEST_F(Tx, QueueTimeErrorsAbort) {
  EXPECT_EQ(run(a, {"MULTI"}), "+OK\r\n");
  EXPECT_EQ(run(a, {"SET", "k", "v"}), "+QUEUED\r\n");
  EXPECT_EQ(run(a, {"NOSUCHCMD"}), "-ERR unknown command 'NOSUCHCMD'\r\n");
  EXPECT_EQ(run(a, {"GET"}), "-ERR wrong number of arguments for 'get' command\r\n");
  EXPECT_EQ(run(a, {"EXEC"}), "-EXECABORT Transaction discarded because of previous errors.\r\n");
  EXPECT_EQ(run(a, {"GET", "k"}), "$-1\r\n");
}

// ...but errors while running don't: Redis has no rollback.
TEST_F(Tx, RuntimeErrorsDontStopTheOthers) {
  run(a, {"MULTI"});
  run(a, {"SET", "s", "v"});
  run(a, {"INCR", "s"});
  run(a, {"SET", "t", "w"});
  EXPECT_EQ(run(a, {"EXEC"}), "*3\r\n+OK\r\n-ERR value is not an integer or out of range\r\n+OK\r\n");
  EXPECT_EQ(run(a, {"GET", "t"}), "$1\r\nw\r\n");
}

TEST_F(Tx, WatchedKeyChangedMeansNoExec) {
  run(a, {"SET", "k", "1"});
  EXPECT_EQ(run(a, {"WATCH", "k"}), "+OK\r\n");
  run(b, {"SET", "k", "2"});
  run(a, {"MULTI"});
  run(a, {"SET", "k", "3"});
  EXPECT_EQ(run(a, {"EXEC"}), "*-1\r\n");
  EXPECT_EQ(run(a, {"GET", "k"}), "$1\r\n2\r\n");
}

TEST_F(Tx, WatchedKeyUnchangedMeansExec) {
  run(a, {"SET", "k", "2"});
  run(a, {"WATCH", "k"});
  run(a, {"MULTI"});
  run(a, {"INCR", "k"});
  EXPECT_EQ(run(a, {"EXEC"}), "*1\r\n:3\r\n");
}

TEST_F(Tx, ExecUnwatches) {
  run(a, {"WATCH", "k"});
  run(a, {"MULTI"});
  run(a, {"EXEC"});
  run(b, {"SET", "k", "x"});
  run(a, {"MULTI"});
  run(a, {"GET", "k"});
  EXPECT_EQ(run(a, {"EXEC"}), "*1\r\n$1\r\nx\r\n") << "the watch ended with the last EXEC";
}

TEST_F(Tx, Unwatch) {
  run(a, {"WATCH", "k"});
  EXPECT_EQ(run(a, {"UNWATCH"}), "+OK\r\n");
  run(b, {"SET", "k", "9"});
  run(a, {"MULTI"});
  run(a, {"GET", "k"});
  EXPECT_EQ(run(a, {"EXEC"}), "*1\r\n$1\r\n9\r\n");
}

TEST_F(Tx, WatchInsideMultiIsRefused) {
  run(a, {"MULTI"});
  EXPECT_EQ(run(a, {"WATCH", "k"}), "-ERR WATCH inside MULTI is not allowed\r\n");
  run(a, {"SET", "k", "v"});
  EXPECT_EQ(run(a, {"EXEC"}), "*1\r\n+OK\r\n") << "a refused WATCH doesn't abort the transaction";
}

// The pitfall: "the key changed" includes deletion, expiry by a command, renames and
// flushes, and changes made by the watching client itself.
TEST_F(Tx, EveryKindOfChangeCounts) {
  for (Args change : std::vector<Args>{{"DEL", "k"}, {"RENAME", "k", "other"}, {"FLUSHALL"}, {"APPEND", "k", "x"}}) {
    run(a, {"SET", "k", "1"});
    run(a, {"WATCH", "k"});
    run(b, change);
    run(a, {"MULTI"});
    run(a, {"PING"});
    EXPECT_EQ(run(a, {"EXEC"}), "*-1\r\n") << change[0] << " didn't invalidate the WATCH";
  }
  run(a, {"SET", "k", "1"});
  run(a, {"WATCH", "k"});
  run(a, {"SET", "k", "2"});  // the watcher itself
  run(a, {"MULTI"});
  EXPECT_EQ(run(a, {"EXEC"}), "*-1\r\n");
}

TEST_F(Tx, ForgetDropsWatches) {
  {
    FakeSession gone;
    run(gone, {"WATCH", "k"});
    run(gone, {"MULTI"});
    tx.forget(gone);
  }
  run(b, {"SET", "k", "v"});  // must not touch the forgotten session
  EXPECT_EQ(run(b, {"GET", "k"}), "$1\r\nv\r\n");
}

TEST_F(Tx, ExecutingFlag) {
  table.add({"probe", 1, [this](CommandContext& c) { c.reply += tx.executing() ? "+yes\r\n" : "+no\r\n"; }});
  EXPECT_EQ(run(a, {"PROBE"}), "+no\r\n");
  run(a, {"MULTI"});
  run(a, {"PROBE"});
  EXPECT_EQ(run(a, {"EXEC"}), "*1\r\n+yes\r\n");
}

}  // namespace
}  // namespace rd
