#include "net/timers/timers.h"

#include <algorithm>
#include <utility>

namespace rd {

namespace {

// std heap functions build a max-heap; "greater" puts the smallest (when, id) on top.
struct Later {
  template <class E>
  bool operator()(const E& a, const E& b) const {
    return a.when != b.when ? a.when > b.when : a.id > b.id;
  }
};

}  // namespace

TimerId Timers::add(int64_t when_ms, TimerCallback cb) {
  TimerId id = next_id_++;
  live_.emplace(id, std::make_pair(when_ms, std::move(cb)));
  heap_.push_back({when_ms, id});
  std::push_heap(heap_.begin(), heap_.end(), Later{});
  return id;
}

bool Timers::cancel(TimerId id) { return live_.erase(id) > 0; }

size_t Timers::size() const { return live_.size(); }

std::optional<int64_t> Timers::next_deadline() {
  while (!heap_.empty()) {
    const Entry& top = heap_.front();
    auto it = live_.find(top.id);
    if (it != live_.end() && it->second.first == top.when) return top.when;
    std::pop_heap(heap_.begin(), heap_.end(), Later{});
    heap_.pop_back();
  }
  return std::nullopt;
}

int64_t Timers::ms_until_next(int64_t now_ms) {
  auto next = next_deadline();
  if (!next) return -1;
  return std::max<int64_t>(0, *next - now_ms);
}

size_t Timers::run_due(int64_t now_ms) {
  const TimerId max_id = next_id_ - 1;
  std::vector<Entry> again;  // rescheduled this call: pushed after the loop
  size_t ran = 0;
  for (;;) {
    auto next = next_deadline();
    if (!next || *next > now_ms) break;
    Entry e = heap_.front();
    std::pop_heap(heap_.begin(), heap_.end(), Later{});
    heap_.pop_back();
    if (e.id > max_id) {
      again.push_back(e);  // added during this call: keep it for later
      continue;
    }
    TimerCallback cb = std::move(live_[e.id].second);
    int64_t d = cb(e.id);
    ++ran;
    auto it = live_.find(e.id);
    if (it == live_.end()) continue;  // cancelled by its own callback
    if (d == kNoMore) {
      live_.erase(it);
    } else {
      it->second = {now_ms + d, std::move(cb)};
      again.push_back({now_ms + d, e.id});
    }
  }
  for (const Entry& e : again) {
    heap_.push_back(e);
    std::push_heap(heap_.begin(), heap_.end(), Later{});
  }
  return ran;
}

}  // namespace rd
