#include "persist/aof_rewrite/aof_rewrite.h"

#include <gtest/gtest.h>
#include <unistd.h>

#include <chrono>
#include <fstream>
#include <map>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include "db/expire/expire.h"
#include "db/hashes/hashes.h"
#include "db/lists/lists.h"
#include "db/sets/sets.h"
#include "db/zsets/zsets.h"

namespace rd {
namespace {

using Args = std::vector<std::string>;

std::string read_file(const std::string& path) {
  std::ifstream f(path, std::ios::binary);
  std::stringstream ss;
  ss << f.rdbuf();
  return ss.str();
}

// A server's worth of keyspace + commands.
struct Db {
  explicit Db(const Clock& clock) : ks(clock) {
    register_keyspace_commands(table, ks);
    register_expire_commands(table, ks);
    register_list_commands(table, ks);
    register_hash_commands(table, ks);
    register_set_commands(table, ks);
    register_zset_commands(table, ks);
  }
  std::string run(Args args) {
    std::string reply;
    table.dispatch(args, reply);
    return reply;
  }
  // Everything observable, in a canonical form, for comparing two keyspaces.
  std::map<std::string, std::string> dump() {
    std::map<std::string, std::string> out;
    std::vector<std::string> keys;
    ks.entries().for_each([&](const std::string& k, Keyspace::Entry&) { keys.push_back(k); });
    for (auto& k : keys) {
      std::string type = run({"TYPE", k});
      std::string v;
      if (type == "+string\r\n") v = run({"GET", k});
      if (type == "+list\r\n") v = run({"LRANGE", k, "0", "-1"});
      if (type == "+hash\r\n") v = run({"HLEN", k}) + run({"HGET", k, "f7"});
      if (type == "+set\r\n") v = run({"SCARD", k}) + run({"SISMEMBER", k, "m7"});
      if (type == "+zset\r\n") v = run({"ZRANGE", k, "0", "-1", "WITHSCORES"});
      out[k] = type + v + run({"PEXPIRETIME", k});
    }
    return out;
  }
  Keyspace ks;
  CommandTable table;
};

class Rewrite : public ::testing::Test {
 protected:
  Rewrite() {
    char dir[] = "/tmp/aofrwXXXXXX";
    dir_ = ::mkdtemp(dir);
    path_ = dir_ + "/appendonly.aof";
  }
  ~Rewrite() override {
    std::remove(path_.c_str());
    std::remove((path_ + ".rewrite").c_str());
    ::rmdir(dir_.c_str());
  }
  void fill(Db& db) {
    db.run({"SET", "s", "hello"});
    db.run({"SET", "n", "41"});
    db.run({"INCR", "n"});
    std::vector<std::string> rpush = {"RPUSH", "l"}, hset = {"HSET", "h"}, sadd = {"SADD", "set"}, zadd = {"ZADD", "z"};
    for (int i = 0; i < 200; ++i) {
      rpush.push_back("e" + std::to_string(i));
      hset.push_back("f" + std::to_string(i));
      hset.push_back("v" + std::to_string(i));
      sadd.push_back("m" + std::to_string(i));
      zadd.push_back(std::to_string(i) + ".5");
      zadd.push_back("z" + std::to_string(i));
    }
    db.run(rpush);
    db.run(hset);
    db.run(sadd);
    db.run(zadd);
    db.run({"SET", "ttl", "v", "PX", "50000"});
  }
  FakeClock clock_{1'800'000'000'000};
  std::string dir_, path_;
};

TEST_F(Rewrite, ReplayingTheRewriteRebuildsTheKeyspace) {
  Db a(clock_);
  fill(a);
  std::string log = rewrite_commands(a.ks);
  Db b(clock_);
  ReplayResult r = replay_aof(log, b.table);
  ASSERT_TRUE(r.ok) << r.error;
  EXPECT_FALSE(r.truncated);
  EXPECT_EQ(b.dump(), a.dump());
}

TEST_F(Rewrite, AggregatesAreBatchedBy64) {
  Db a(clock_);
  fill(a);
  std::string log = rewrite_commands(a.ks);
  // 200 elements -> 64 + 64 + 64 + 8: four commands each for l, h, set, z; one SET each
  // for s, n, ttl; one PEXPIREAT for ttl.
  Db b(clock_);
  EXPECT_EQ(replay_aof(log, b.table).commands, 4u * 4 + 3 + 1);
  EXPECT_EQ(log.find("INCR"), std::string::npos) << "history is gone: only the final value matters";
}

TEST_F(Rewrite, ExpiredKeysAreLeftOut) {
  Db a(clock_);
  a.run({"SET", "dead", "v", "PX", "10"});
  a.run({"SET", "alive", "v"});
  clock_.advance(10);
  std::string log = rewrite_commands(a.ks);
  EXPECT_EQ(log.find("dead"), std::string::npos);
  EXPECT_NE(log.find("alive"), std::string::npos);
}

// The full thing: a log of 2000 INCRs is compacted to one SET; writes made while the child
// was working are kept; the AOF keeps working afterwards.
TEST_F(Rewrite, BackgroundRewrite) {
  SystemClock clock;
  Db db(clock);
  EventLoop loop(clock);
  Aof aof(path_, FsyncPolicy::kNo, clock);
  ASSERT_TRUE(aof.open());
  for (int i = 0; i < 2000; ++i) {
    std::string reply;
    dispatch_logged(db.table, db.ks, aof, Args{"INCR", "n"}, reply);
  }
  aof.flush();
  size_t before = read_file(path_).size();

  AofRewriter rw(db.ks, aof, loop);
  ASSERT_TRUE(rw.start());
  EXPECT_FALSE(rw.start()) << "one rewrite at a time";
  std::string reply;
  dispatch_logged(db.table, db.ks, aof, Args{"SET", "during", "x"}, reply);  // the child can't see this
  aof.flush();
  auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
  while (rw.in_progress() && std::chrono::steady_clock::now() < deadline) loop.process_events(10);
  ASSERT_FALSE(rw.in_progress());
  EXPECT_EQ(rw.last_status(), true);
  dispatch_logged(db.table, db.ks, aof, Args{"SET", "after", "y"}, reply);
  aof.flush();

  std::string now = read_file(path_);
  EXPECT_LT(now.size(), before / 10) << "the rewrite didn't compact the log";
  Db replayed(clock);
  ASSERT_TRUE(replay_aof(now, replayed.table).ok);
  EXPECT_EQ(replayed.run({"GET", "n"}), "$4\r\n2000\r\n");
  EXPECT_EQ(replayed.run({"GET", "during"}), "$1\r\nx\r\n") << "a write made during the rewrite was lost";
  EXPECT_EQ(replayed.run({"GET", "after"}), "$1\r\ny\r\n") << "the AOF wasn't reopened on the new file";
}

TEST_F(Rewrite, Command) {
  SystemClock clock;
  Db db(clock);
  EventLoop loop(clock);
  Aof aof(path_, FsyncPolicy::kNo, clock);
  ASSERT_TRUE(aof.open());
  AofRewriter rw(db.ks, aof, loop);
  register_rewrite_command(db.table, rw);
  EXPECT_EQ(db.run({"BGREWRITEAOF"}), "+Background append only file rewriting started\r\n");
  EXPECT_EQ(db.run({"BGREWRITEAOF"}), "-ERR Background append only file rewriting already in progress\r\n");
  auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
  while (rw.in_progress() && std::chrono::steady_clock::now() < deadline) loop.process_events(10);
}

}  // namespace
}  // namespace rd
