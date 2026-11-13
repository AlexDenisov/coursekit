#include "persist/bgsave/bgsave.h"

#include <gtest/gtest.h>
#include <unistd.h>

#include <chrono>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>

#include "common/transcript.h"
#include "persist/rdb_writer/rdb_test_parser.h"

namespace rd {
namespace {

using testing::play;

std::string read_file(const std::string& path) {
  std::ifstream f(path, std::ios::binary);
  std::stringstream ss;
  ss << f.rdbuf();
  return ss.str();
}

class BgSave : public ::testing::Test {
 protected:
  BgSave() {
    char dir[] = "/tmp/bgsaveXXXXXX";
    dir_ = ::mkdtemp(dir);
    path_ = dir_ + "/dump.rdb";
    saver_ = std::make_unique<BackgroundSaver>(ks_, loop_, path_);
    register_keyspace_commands(table_, ks_);
    register_save_commands(table_, *saver_);
  }
  ~BgSave() override {
    std::remove(path_.c_str());
    ::rmdir(dir_.c_str());
  }
  // Runs the loop until the save finishes (or 5 s).
  void wait_for_save() {
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (saver_->in_progress() && std::chrono::steady_clock::now() < deadline) loop_.process_events(10);
    ASSERT_FALSE(saver_->in_progress()) << "the child was never reaped";
  }
  SystemClock clock_;
  Keyspace ks_{clock_};
  EventLoop loop_{clock_};
  std::string dir_, path_;
  std::unique_ptr<BackgroundSaver> saver_;
  CommandTable table_;
};

TEST_F(BgSave, SaveNow) {
  ks_.set("k", std::make_unique<StringValue>("v"));
  ASSERT_TRUE(saver_->save_now());
  auto p = rdb::testing::parse(read_file(path_));
  ASSERT_EQ(p.error, "");
  EXPECT_EQ(p.keys["k"].items[0], "v");
  EXPECT_EQ(saver_->last_status(), true);
  EXPECT_GT(saver_->last_save_ms(), 0);
}

TEST_F(BgSave, BackgroundSaveWritesTheFile) {
  for (int i = 0; i < 1000; ++i) ks_.set("k" + std::to_string(i), std::make_unique<StringValue>(std::to_string(i)));
  ASSERT_TRUE(saver_->start());
  EXPECT_TRUE(saver_->in_progress());
  EXPECT_FALSE(saver_->start()) << "one save at a time";
  wait_for_save();
  EXPECT_EQ(saver_->last_status(), true);
  auto p = rdb::testing::parse(read_file(path_));
  ASSERT_EQ(p.error, "");
  EXPECT_EQ(p.keys.size(), 1000u);
}

// The point of fork(): the child sees the keyspace as it was at the moment of the fork,
// while the parent keeps changing it. (The kernel copies only the pages the parent
// touches: copy-on-write.)
TEST_F(BgSave, TheSnapshotIsThePointInTimeOfTheFork) {
  for (int i = 0; i < 20000; ++i) ks_.set("k" + std::to_string(i), std::make_unique<StringValue>("before"));
  ASSERT_TRUE(saver_->start());
  for (int i = 0; i < 20000; ++i) static_cast<StringValue*>(ks_.find("k" + std::to_string(i)))->s = "after";
  ks_.set("new", std::make_unique<StringValue>("x"));
  wait_for_save();
  auto p = rdb::testing::parse(read_file(path_));
  ASSERT_EQ(p.error, "");
  EXPECT_EQ(p.keys.size(), 20000u);
  EXPECT_EQ(p.keys.count("new"), 0u);
  EXPECT_EQ(p.keys["k123"].items[0], "before");
  EXPECT_EQ(static_cast<StringValue*>(ks_.find("k123"))->s, "after");
}

TEST_F(BgSave, FailureIsReported) {
  BackgroundSaver bad(ks_, loop_, "/nonexistent/dir/dump.rdb");
  ASSERT_TRUE(bad.start());
  auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
  while (bad.in_progress() && std::chrono::steady_clock::now() < deadline) loop_.process_events(10);
  EXPECT_EQ(bad.last_status(), false);
  EXPECT_FALSE(bad.save_now());
}

// Replies recorded from Redis 8.10 (tools/cases/persist.txt).
TEST_F(BgSave, Commands) {
  play(table_, {
      {{"SET", "k", "v"}, "+OK\r\n"},
      {{"LASTSAVE"}, ":0\r\n"},
      {{"SAVE"}, "+OK\r\n"},
      {{"BGSAVE"}, "+Background saving started\r\n"},
      {{"BGSAVE"}, "-ERR Background save already in progress\r\n"},
      {{"SAVE"}, "-ERR Background save already in progress\r\n"},
  });
  wait_for_save();
  std::string reply;
  table_.dispatch(std::vector<std::string>{"LASTSAVE"}, reply);
  EXPECT_EQ(reply, ":" + std::to_string(saver_->last_save_ms() / 1000) + "\r\n");
}

}  // namespace
}  // namespace rd
