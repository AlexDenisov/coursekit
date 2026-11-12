#pragma once

// net/timers: Redis's "time events" (ae.c), as a min-heap. The spec is this file; the
// story is README.md.

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <unordered_map>
#include <vector>

namespace rd {

using TimerId = uint64_t;

// A callback's return value: run again `d >= 0` ms after now, or never (kNoMore).
inline constexpr int64_t kNoMore = -1;

using TimerCallback = std::function<int64_t(TimerId)>;

class Timers {
 public:
  // Schedules cb at absolute time when_ms. Ids start at 1 and increase. O(log n).
  TimerId add(int64_t when_ms, TimerCallback cb);

  // Removes a pending timer. False if it doesn't exist (never added, already finished,
  // already cancelled). O(log n) amortised; may be called from inside a callback.
  bool cancel(TimerId id);

  // Pending timers.
  size_t size() const;

  // The earliest deadline, if any. O(1) amortised.
  std::optional<int64_t> next_deadline();

  // How long an event loop may sleep at now_ms: 0 if a timer is due, the ms until the
  // earliest deadline, or -1 if there are no timers ("forever").
  int64_t ms_until_next(int64_t now_ms);

  // Runs every timer with deadline <= now_ms, earliest deadline first, ties in the order
  // they were added (by id). A callback returning d >= 0 is rescheduled for now_ms + d;
  // it runs at most once per call, even with d == 0. Timers added by callbacks during
  // this call don't run in this call; timers cancelled by a callback don't run at all.
  // Returns the number of callbacks run.
  size_t run_due(int64_t now_ms);

 private:
  struct Entry {
    int64_t when;
    TimerId id;
  };
  // Min-heap by (when, id); may hold stale entries for cancelled or rescheduled timers.
  std::vector<Entry> heap_;
  // The live timers: id -> (current deadline, callback).
  std::unordered_map<TimerId, std::pair<int64_t, TimerCallback>> live_;
  TimerId next_id_ = 1;
};

}  // namespace rd
