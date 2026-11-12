#include "net/timers/timers.h"

#include <algorithm>

namespace rd {

TimerId Timers::add(int64_t when_ms, TimerCallback cb) {
  // TODO(you): record it in live_ and push an Entry (std::push_heap with a comparator
  // that makes the smallest (when, id) the top).
  return 0;
}

bool Timers::cancel(TimerId id) {
  // TODO(you): erase from live_; leave the heap entry, skip it when it surfaces.
  return false;
}

size_t Timers::size() const {
  // TODO(you): live timers only.
  return 0;
}

std::optional<int64_t> Timers::next_deadline() {
  // TODO(you): pop stale entries off the top first (cancelled, or rescheduled: the
  // entry's when differs from live_'s).
  return std::nullopt;
}

int64_t Timers::ms_until_next(int64_t now_ms) {
  // TODO(you): from next_deadline.
  return -1;
}

size_t Timers::run_due(int64_t now_ms) {
  // TODO(you): remember the highest id that exists now; pop due entries; skip stale
  // ones and ids added during this call; run the callback (careful: it may cancel or
  // add timers); reschedule or forget it.
  return 0;
}

}  // namespace rd
