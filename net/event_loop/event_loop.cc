#include "net/event_loop/event_loop.h"

#include <sys/event.h>
#include <unistd.h>

#include <cerrno>
#include <vector>

#include "common/check.h"

namespace rd {

EventLoop::EventLoop(const Clock& clock) : clock_(clock) {
  // TODO(you): kqueue(); pipe() for waking up, both ends non-blocking, and watch the read
  // end (its handler drains the pipe).
}

EventLoop::~EventLoop() {
  // TODO(you): close the kqueue.
}

bool EventLoop::add(int fd, int mask, FileHandler handler) {
  // TODO(you): one kevent change per event (EVFILT_READ / EVFILT_WRITE, EV_ADD); then
  // remember the handler(s) in watches_.
  return false;
}

void EventLoop::remove(int fd, int mask) {
  // TODO(you): EV_DELETE the watched events; update or erase watches_.
}

int EventLoop::mask(int fd) const {
  // TODO(you): from watches_.
  return 0;
}

int EventLoop::process_events(int max_wait_ms) {
  // TODO(you): before_sleep; compute the timeout from timers_.ms_until_next(now); kevent;
  // for each returned event, check it's *still* watched before calling its handler
  // (copy the handler first: it may remove itself); then timers_.run_due(now).
  return 0;
}

void EventLoop::run() {
  // TODO(you): loop until stop_; reset stop_ when done so run() can be used again.
}

void EventLoop::stop() {
  // TODO(you): set the flag; write a byte to the wake-up pipe.
}

}  // namespace rd
