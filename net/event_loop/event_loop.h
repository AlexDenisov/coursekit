#pragma once

// net/event_loop: Redis's ae.c on kqueue. The spec is this file; the story is README.md.

#include <atomic>
#include <cstdint>
#include <functional>
#include <unordered_map>

#include "common/clock.h"
#include "net/sockets/sockets.h"
#include "net/timers/timers.h"

namespace rd {

inline constexpr int kReadable = 1;
inline constexpr int kWritable = 2;

// Called with the fd and the single event that fired (kReadable or kWritable).
using FileHandler = std::function<void(int fd, int event)>;

class EventLoop {
 public:
  // Creates the kqueue, and a wake-up pipe registered for reading so stop() works from
  // any thread. `clock` drives the timers.
  explicit EventLoop(const Clock& clock);
  ~EventLoop();
  EventLoop(const EventLoop&) = delete;
  EventLoop& operator=(const EventLoop&) = delete;

  // Watches fd for `mask` (kReadable, kWritable or both), calling `handler` for those
  // events; replaces any handler previously set for them. Other events on fd keep their
  // handlers. False if the kernel refuses (e.g. a closed fd).
  bool add(int fd, int mask, FileHandler handler);

  // Stops watching fd for `mask`. Safe for events not being watched. Takes effect at
  // once, also for events already returned by the kernel and not yet handled.
  void remove(int fd, int mask);

  // The events fd is watched for (0 if none).
  int mask(int fd) const;

  Timers& timers() { return timers_; }

  // Runs at the start of every process_events, before waiting (Redis's beforeSleep:
  // flush replies, ...).
  void set_before_sleep(std::function<void()> fn) { before_sleep_ = std::move(fn); }

  // One iteration: before_sleep; wait for file events, at most until the next timer is
  // due, or at most max_wait_ms if there are no timers (-1: no limit; 0: don't wait);
  // run the handlers of the ready events (for an fd, readable before writable); then
  // run the due timers. Returns the number of file events handled plus timers run.
  int process_events(int max_wait_ms);

  // process_events(-1) until stop() is called.
  void run();

  // Makes run() return after the current iteration. May be called from any thread (and
  // from handlers); wakes the loop if it's waiting.
  void stop();

 private:
  struct Watch {
    int mask = 0;
    FileHandler on_read, on_write;
  };
  const Clock& clock_;
  int kq_ = -1;
  Fd wake_read_, wake_write_;
  std::unordered_map<int, Watch> watches_;
  Timers timers_;
  std::function<void()> before_sleep_;
  std::atomic<bool> stop_{false};
};

}  // namespace rd
