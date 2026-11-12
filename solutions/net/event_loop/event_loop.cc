#include "net/event_loop/event_loop.h"

#include <sys/event.h>
#include <unistd.h>

#include <cerrno>
#include <vector>

#include "common/check.h"
#include "net/nonblocking/nonblocking.h"

namespace rd {

EventLoop::EventLoop(const Clock& clock) : clock_(clock) {
  kq_ = ::kqueue();
  RD_CHECK_GE(kq_, 0, "kqueue failed");
  int fds[2];
  RD_CHECK_EQ(::pipe(fds), 0);
  wake_read_.reset(fds[0]);
  wake_write_.reset(fds[1]);
  set_nonblocking(fds[0]);
  set_nonblocking(fds[1]);
  struct kevent ev;
  EV_SET(&ev, fds[0], EVFILT_READ, EV_ADD, 0, 0, nullptr);
  RD_CHECK_EQ(::kevent(kq_, &ev, 1, nullptr, 0, nullptr), 0);
}

EventLoop::~EventLoop() {
  if (kq_ >= 0) ::close(kq_);
}

bool EventLoop::add(int fd, int mask, FileHandler handler) {
  struct kevent changes[2];
  int n = 0;
  if (mask & kReadable) EV_SET(&changes[n++], fd, EVFILT_READ, EV_ADD, 0, 0, nullptr);
  if (mask & kWritable) EV_SET(&changes[n++], fd, EVFILT_WRITE, EV_ADD, 0, 0, nullptr);
  if (::kevent(kq_, changes, n, nullptr, 0, nullptr) != 0) return false;
  Watch& w = watches_[fd];
  w.mask |= mask;
  if (mask & kReadable) w.on_read = handler;
  if (mask & kWritable) w.on_write = handler;
  return true;
}

void EventLoop::remove(int fd, int mask) {
  auto it = watches_.find(fd);
  if (it == watches_.end()) return;
  Watch& w = it->second;
  mask &= w.mask;
  struct kevent changes[2];
  int n = 0;
  if (mask & kReadable) EV_SET(&changes[n++], fd, EVFILT_READ, EV_DELETE, 0, 0, nullptr);
  if (mask & kWritable) EV_SET(&changes[n++], fd, EVFILT_WRITE, EV_DELETE, 0, 0, nullptr);
  if (n > 0) ::kevent(kq_, changes, n, nullptr, 0, nullptr);  // may fail if fd was closed
  w.mask &= ~mask;
  if (mask & kReadable) w.on_read = nullptr;
  if (mask & kWritable) w.on_write = nullptr;
  if (w.mask == 0) watches_.erase(it);
}

int EventLoop::mask(int fd) const {
  auto it = watches_.find(fd);
  return it == watches_.end() ? 0 : it->second.mask;
}

int EventLoop::process_events(int max_wait_ms) {
  if (before_sleep_) before_sleep_();
  int64_t wait = timers_.ms_until_next(clock_.now_ms());
  if (wait < 0 || (max_wait_ms >= 0 && wait > max_wait_ms)) wait = max_wait_ms;
  timespec ts{};
  timespec* tsp = nullptr;
  if (wait >= 0) {
    ts.tv_sec = wait / 1000;
    ts.tv_nsec = (wait % 1000) * 1'000'000;
    tsp = &ts;
  }
  struct kevent events[128];
  int n = ::kevent(kq_, nullptr, 0, events, 128, tsp);
  if (n < 0) n = 0;  // EINTR
  // All readable events first, then all writable ones: readable before writable per fd.
  std::vector<std::pair<int, int>> ready;
  for (int pass : {kReadable, kWritable}) {
    for (int i = 0; i < n; ++i) {
      int ev = events[i].filter == EVFILT_READ ? kReadable : kWritable;
      if (ev == pass) ready.emplace_back(static_cast<int>(events[i].ident), ev);
    }
  }
  int handled = 0;
  for (auto [fd, ev] : ready) {
    if (fd == wake_read_.get()) {
      char buf[64];
      while (::read(fd, buf, sizeof buf) > 0) {
      }
      continue;
    }
    auto it = watches_.find(fd);
    if (it == watches_.end() || !(it->second.mask & ev)) continue;
    FileHandler h = ev == kReadable ? it->second.on_read : it->second.on_write;
    h(fd, ev);
    ++handled;
  }
  handled += static_cast<int>(timers_.run_due(clock_.now_ms()));
  return handled;
}

void EventLoop::run() {
  while (!stop_.load()) process_events(-1);
  stop_.store(false);
}

void EventLoop::stop() {
  stop_.store(true);
  char c = 1;
  (void)!::write(wake_write_.get(), &c, 1);
}

}  // namespace rd
