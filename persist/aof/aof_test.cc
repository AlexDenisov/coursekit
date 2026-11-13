#include "persist/aof/aof.h"

#include <gtest/gtest.h>
#include <unistd.h>

#include <fstream>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include "common/test_client.h"  // encode_request
#include "db/expire/expire.h"
#include "db/lists/lists.h"

namespace rd {
namespace {

using Args = std::vector<std::string>;

std::string read_file(const std::string& path) {
  std::ifstream f(path, std::ios::binary);
  std::stringstream ss;
  ss << f.rdbuf();
  return ss.str();
}

// ---- log_form (the rewrites Redis 8.10 applies; see tools/aof_reference.py) ----

TEST(LogForm, RelativeTtlsBecomeAbsolute) {
  int64_t now = 1'794'519'093'527;
  EXPECT_EQ(log_form(Args{"SET", "t", "v", "EX", "100"}, now), (Args{"SET", "t", "v", "PXAT", "1794519193527"}));
  EXPECT_EQ(log_form(Args{"SET", "p", "v", "PX", "5000"}, now), (Args{"SET", "p", "v", "PXAT", "1794519098527"}));
  EXPECT_EQ(log_form(Args{"set", "p", "v", "nx", "ex", "1"}, now), (Args{"set", "p", "v", "nx", "PXAT", "1794519094527"}));
  EXPECT_EQ(log_form(Args{"SET", "a", "v", "EXAT", "4102444800"}, now), (Args{"SET", "a", "v", "PXAT", "4102444800000"}));
  EXPECT_EQ(log_form(Args{"EXPIRE", "k", "100"}, now), (Args{"PEXPIREAT", "k", "1794519193527"}));
  EXPECT_EQ(log_form(Args{"PEXPIRE", "k", "5000"}, now), (Args{"PEXPIREAT", "k", "1794519098527"}));
  EXPECT_EQ(log_form(Args{"EXPIREAT", "k", "4102444800"}, now), (Args{"PEXPIREAT", "k", "4102444800000"}));
  EXPECT_EQ(log_form(Args{"expire", "k", "10", "NX"}, now), (Args{"PEXPIREAT", "k", "1794519103527", "NX"}));
}

TEST(LogForm, OtherCommandsAreUnchanged) {
  EXPECT_EQ(log_form(Args{"SET", "k", "v"}, 1), (Args{"SET", "k", "v"}));
  EXPECT_EQ(log_form(Args{"SET", "k", "v", "KEEPTTL"}, 1), (Args{"SET", "k", "v", "KEEPTTL"}));
  EXPECT_EQ(log_form(Args{"PEXPIREAT", "k", "123"}, 1), (Args{"PEXPIREAT", "k", "123"}));
  EXPECT_EQ(log_form(Args{"RPUSH", "EX", "PX"}, 1), (Args{"RPUSH", "EX", "PX"}));
}

// ---- the log ----

class AofTest : public ::testing::Test {
 protected:
  AofTest() {
    char dir[] = "/tmp/aofXXXXXX";
    dir_ = ::mkdtemp(dir);
    path_ = dir_ + "/appendonly.aof";
    register_keyspace_commands(table_, ks_);
    register_expire_commands(table_, ks_);
    register_list_commands(table_, ks_);
  }
  ~AofTest() override {
    std::remove(path_.c_str());
    ::rmdir(dir_.c_str());
  }
  std::string run(Aof& aof, Args args) {
    std::string reply;
    dispatch_logged(table_, ks_, aof, args, reply);
    return reply;
  }
  FakeClock clock_{1'794'519'093'527};
  Keyspace ks_{clock_};
  CommandTable table_;
  std::string dir_, path_;
};

// What Redis logged for the same commands, minus its SELECT 0.
TEST_F(AofTest, LogsWritesOnly) {
  Aof aof(path_, FsyncPolicy::kAlways, clock_);
  ASSERT_TRUE(aof.open());
  run(aof, {"SET", "k", "v"});
  run(aof, {"GET", "k"});
  run(aof, {"SET", "t", "v", "EX", "100"});
  run(aof, {"EXPIRE", "k", "100"});
  run(aof, {"INCR", "n"});
  run(aof, {"DEL", "nothere"});
  run(aof, {"DEL", "n"});
  run(aof, {"RPUSH", "l", "a", "b"});
  ASSERT_TRUE(aof.flush());
  EXPECT_EQ(read_file(path_), encode_request({"SET", "k", "v"}) + encode_request({"SET", "t", "v", "PXAT", "1794519193527"}) +
                                  encode_request({"PEXPIREAT", "k", "1794519193527"}) + encode_request({"INCR", "n"}) +
                                  encode_request({"DEL", "n"}) + encode_request({"RPUSH", "l", "a", "b"}));
}

TEST_F(AofTest, NothingReachesTheFileBeforeFlush) {
  Aof aof(path_, FsyncPolicy::kAlways, clock_);
  ASSERT_TRUE(aof.open());
  run(aof, {"SET", "k", "v"});
  EXPECT_EQ(read_file(path_), "");
  aof.flush();
  EXPECT_EQ(read_file(path_), encode_request({"SET", "k", "v"}));
}

TEST_F(AofTest, FsyncPolicies) {
  Aof always(path_ + ".a", FsyncPolicy::kAlways, clock_), everysec(path_ + ".e", FsyncPolicy::kEverySec, clock_),
      never(path_ + ".n", FsyncPolicy::kNo, clock_);
  for (Aof* a : {&always, &everysec, &never}) ASSERT_TRUE(a->open());
  for (int i = 0; i < 5; ++i) {
    for (Aof* a : {&always, &everysec, &never}) {
      run(*a, {"INCR", "n"});
      a->flush();
      a->cron();
    }
    clock_.advance(300);
  }
  EXPECT_EQ(always.fsync_count(), 5u);
  EXPECT_EQ(everysec.fsync_count(), 1u) << "1.5 s of writes: one fsync after the first second";
  EXPECT_EQ(never.fsync_count(), 0u);
  for (auto s : {".a", ".e", ".n"}) std::remove((path_ + s).c_str());
}

TEST_F(AofTest, ReplayRestoresTheData) {
  {
    Aof aof(path_, FsyncPolicy::kAlways, clock_);
    ASSERT_TRUE(aof.open());
    run(aof, {"SET", "k", "v"});
    run(aof, {"SET", "t", "v", "EX", "100"});
    run(aof, {"RPUSH", "l", "a", "b", "c"});
    run(aof, {"INCR", "n"});
    run(aof, {"INCR", "n"});
    aof.flush();
  }
  clock_.advance(60'000);  // a minute later, a fresh server
  Keyspace fresh(clock_);
  CommandTable t;
  register_keyspace_commands(t, fresh);
  register_expire_commands(t, fresh);
  register_list_commands(t, fresh);
  ReplayResult r = load_aof_file(path_, t);
  ASSERT_TRUE(r.ok) << r.error;
  EXPECT_EQ(r.commands, 5u);
  EXPECT_FALSE(r.truncated);
  std::string reply;
  t.dispatch(Args{"GET", "n"}, reply);
  EXPECT_EQ(reply, "$1\r\n2\r\n");
  EXPECT_EQ(fresh.get_expire("t"), 1794519193527) << "the TTL must be absolute, not 100 s after the replay";
}

TEST_F(AofTest, MissingFileIsAnEmptyLog) {
  ReplayResult r = load_aof_file(dir_ + "/nothing.aof", table_);
  EXPECT_TRUE(r.ok);
  EXPECT_EQ(r.commands, 0u);
}

// The pitfall: the server died in the middle of writing a request. Everything before it
// is good; the half request must be dropped, and the file cut back, or every later
// append would be glued to garbage.
TEST_F(AofTest, TruncatedTailIsDroppedAndCut) {
  std::string good = encode_request({"SET", "a", "1"}) + encode_request({"SET", "b", "2"});
  std::string half = encode_request({"SET", "c", "3"}).substr(0, 10);
  {
    std::ofstream f(path_, std::ios::binary);
    f << good << half;
  }
  ReplayResult r = load_aof_file(path_, table_);
  ASSERT_TRUE(r.ok) << r.error;
  EXPECT_TRUE(r.truncated);
  EXPECT_EQ(r.commands, 2u);
  EXPECT_EQ(r.valid_bytes, good.size());
  EXPECT_EQ(read_file(path_), good) << "the file should be cut back to its valid prefix";
  EXPECT_NE(ks_.find("b"), nullptr);
  EXPECT_EQ(ks_.find("c"), nullptr);
}

TEST_F(AofTest, CorruptionInTheMiddleIsAnError) {
  std::string data = encode_request({"SET", "a", "1"}) + "*1\r\n:5\r\n" + encode_request({"SET", "b", "2"});
  ReplayResult r = replay_aof(data, table_);
  EXPECT_FALSE(r.ok);
  EXPECT_FALSE(r.error.empty());
}

TEST_F(AofTest, AppendsAfterReopen) {
  Aof aof(path_, FsyncPolicy::kAlways, clock_);
  ASSERT_TRUE(aof.open());
  run(aof, {"SET", "a", "1"});
  aof.flush();
  ASSERT_TRUE(aof.reopen());
  run(aof, {"SET", "b", "2"});
  aof.flush();
  EXPECT_EQ(read_file(path_), encode_request({"SET", "a", "1"}) + encode_request({"SET", "b", "2"}));
}

TEST_F(AofTest, CaptureSeesEveryAppend) {
  Aof aof(path_, FsyncPolicy::kNo, clock_);
  ASSERT_TRUE(aof.open());
  std::string captured;
  run(aof, {"SET", "a", "1"});
  aof.set_capture(&captured);
  run(aof, {"SET", "b", "2", "EX", "1"});
  aof.set_capture(nullptr);
  run(aof, {"SET", "c", "3"});
  EXPECT_EQ(captured, encode_request({"SET", "b", "2", "PXAT", "1794519094527"}));
}

}  // namespace
}  // namespace rd
