#include "net/timers/timers.h"

#include <gtest/gtest.h>

#include <map>
#include <string>
#include <vector>

#include "common/rng.h"

namespace rd {
namespace {

TEST(Timers, Empty) {
  Timers t;
  EXPECT_EQ(t.size(), 0u);
  EXPECT_EQ(t.next_deadline(), std::nullopt);
  EXPECT_EQ(t.ms_until_next(100), -1);
  EXPECT_EQ(t.run_due(1'000'000), 0u);
}

TEST(Timers, IdsStartAtOneAndIncrease) {
  Timers t;
  EXPECT_EQ(t.add(10, [](TimerId) { return kNoMore; }), 1u);
  EXPECT_EQ(t.add(5, [](TimerId) { return kNoMore; }), 2u);
  EXPECT_EQ(t.size(), 2u);
}

TEST(Timers, RunsOnlyWhatIsDueInDeadlineOrder) {
  Timers t;
  std::string log;
  t.add(30, [&](TimerId) { log += "c"; return kNoMore; });
  t.add(10, [&](TimerId) { log += "a"; return kNoMore; });
  t.add(20, [&](TimerId) { log += "b"; return kNoMore; });
  EXPECT_EQ(t.next_deadline(), 10);
  EXPECT_EQ(t.ms_until_next(4), 6);
  EXPECT_EQ(t.run_due(9), 0u);
  EXPECT_EQ(t.run_due(20), 2u);
  EXPECT_EQ(log, "ab");
  EXPECT_EQ(t.ms_until_next(25), 5);
  EXPECT_EQ(t.ms_until_next(31), 0) << "overdue means don't sleep";
  EXPECT_EQ(t.run_due(100), 1u);
  EXPECT_EQ(log, "abc");
  EXPECT_EQ(t.size(), 0u);
}

TEST(Timers, TiesRunInTheOrderAdded) {
  Timers t;
  std::string log;
  for (char c : std::string("abcdef")) t.add(7, [&log, c](TimerId) { log += c; return kNoMore; });
  t.run_due(7);
  EXPECT_EQ(log, "abcdef");
}

// Redis's serverCron returns 1000/hz from its callback and so runs forever.
TEST(Timers, PeriodicTimerReschedulesFromNow) {
  Timers t;
  int runs = 0;
  t.add(100, [&](TimerId) { ++runs; return 100; });
  t.run_due(100);
  EXPECT_EQ(t.next_deadline(), 200);
  t.run_due(250);  // late: next is 250 + 100, not 300
  EXPECT_EQ(t.next_deadline(), 350);
  EXPECT_EQ(runs, 2);
  EXPECT_EQ(t.size(), 1u);
}

// The pitfall: a timer that asks to run again "now" must not loop forever in one call.
TEST(Timers, ZeroDelayRunsOncePerCall) {
  Timers t;
  int runs = 0;
  t.add(0, [&](TimerId) { ++runs; return 0; });
  EXPECT_EQ(t.run_due(5), 1u);
  EXPECT_EQ(t.run_due(5), 1u);
  EXPECT_EQ(runs, 2);
}

TEST(Timers, Cancel) {
  Timers t;
  bool ran = false;
  TimerId id = t.add(10, [&](TimerId) { ran = true; return kNoMore; });
  EXPECT_TRUE(t.cancel(id));
  EXPECT_FALSE(t.cancel(id));
  EXPECT_FALSE(t.cancel(999));
  EXPECT_EQ(t.size(), 0u);
  EXPECT_EQ(t.next_deadline(), std::nullopt) << "a cancelled timer still counts as a deadline";
  t.run_due(100);
  EXPECT_FALSE(ran);
}

TEST(Timers, FinishedTimerCantBeCancelled) {
  Timers t;
  TimerId id = t.add(1, [](TimerId) { return kNoMore; });
  t.run_due(1);
  EXPECT_FALSE(t.cancel(id));
}

// The pitfall: callbacks change the set of timers while you're iterating over it.
TEST(Timers, CallbackCancelsAnotherDueTimer) {
  Timers t;
  std::string log;
  TimerId second = 0;
  t.add(5, [&](TimerId) { log += "a"; t.cancel(second); return kNoMore; });
  second = t.add(5, [&](TimerId) { log += "b"; return kNoMore; });
  EXPECT_EQ(t.run_due(10), 1u);
  EXPECT_EQ(log, "a");
}

TEST(Timers, CallbackCancelsItself) {
  Timers t;
  int runs = 0;
  t.add(5, [&](TimerId self) { ++runs; t.cancel(self); return 10; });
  t.run_due(5);
  t.run_due(100);
  EXPECT_EQ(runs, 1);
  EXPECT_EQ(t.size(), 0u);
}

TEST(Timers, TimerAddedByACallbackWaitsForTheNextCall) {
  Timers t;
  std::string log;
  t.add(5, [&](TimerId) {
    log += "a";
    t.add(0, [&](TimerId) { log += "b"; return kNoMore; });
    return kNoMore;
  });
  EXPECT_EQ(t.run_due(10), 1u);
  EXPECT_EQ(log, "a");
  EXPECT_EQ(t.run_due(10), 1u);
  EXPECT_EQ(log, "ab");
}

// The oracle: a std::multimap of (deadline, id). Random adds, cancels and runs.
TEST(Timers, MatchesAModel) {
  Rng rng(3);
  Timers t;
  std::map<std::pair<int64_t, TimerId>, int> model;  // (when, id) -> payload
  std::map<TimerId, int64_t> when_of;
  std::vector<int> ran, expected;
  int64_t now = 0;
  for (int step = 0; step < 20000; ++step) {
    double r = rng.uniform();
    if (r < 0.5) {
      int64_t when = now + static_cast<int64_t>(rng.below(1000));
      int payload = step;
      TimerId id = t.add(when, [&ran, payload](TimerId) { ran.push_back(payload); return kNoMore; });
      model[{when, id}] = payload;
      when_of[id] = when;
    } else if (r < 0.7 && !when_of.empty()) {
      auto it = when_of.begin();
      std::advance(it, static_cast<long>(rng.below(when_of.size())));
      ASSERT_TRUE(t.cancel(it->first));
      model.erase({it->second, it->first});
      when_of.erase(it);
    } else {
      now += static_cast<int64_t>(rng.below(50));
      while (!model.empty() && model.begin()->first.first <= now) {
        expected.push_back(model.begin()->second);
        when_of.erase(model.begin()->first.second);
        model.erase(model.begin());
      }
      t.run_due(now);
      ASSERT_EQ(ran, expected) << "at step " << step;
    }
    ASSERT_EQ(t.size(), model.size());
  }
}

// The pitfall: a sorted list makes add or cancel O(n). With 200 000 timers that's
// ~10^10 steps; a heap with lazy deletion is O(log n) each.
TEST(Timers, ManyTimersAreCheap) {
  Timers t;
  std::vector<TimerId> ids;
  for (int i = 0; i < 200000; ++i) {
    ids.push_back(t.add((i * 7919) % 100000, [](TimerId) { return kNoMore; }));
  }
  for (size_t i = 0; i < ids.size(); i += 2) t.cancel(ids[i]);
  EXPECT_EQ(t.size(), 100000u);
  EXPECT_EQ(t.run_due(1'000'000), 100000u);
}

}  // namespace
}  // namespace rd
