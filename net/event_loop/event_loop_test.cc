#include "net/event_loop/event_loop.h"

#include <fcntl.h>
#include <sys/socket.h>
#include <unistd.h>

#include <gtest/gtest.h>

#include <chrono>
#include <string>
#include <thread>
#include <vector>

namespace rd {
namespace {

struct Pair {
  Pair() {
    int fds[2];
    EXPECT_EQ(::socketpair(AF_UNIX, SOCK_STREAM, 0, fds), 0);
    a = fds[0];
    b = fds[1];
    ::fcntl(a, F_SETFL, ::fcntl(a, F_GETFL) | O_NONBLOCK);
  }
  ~Pair() {
    ::close(a);
    ::close(b);
  }
  int a, b;
};

int64_t elapsed_ms(std::chrono::steady_clock::time_point t0) {
  return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0)
      .count();
}

TEST(EventLoop, ReadableWhenDataArrives) {
  SystemClock clock;
  EventLoop loop(clock);
  Pair p;
  std::string got;
  ASSERT_TRUE(loop.add(p.a, kReadable, [&](int fd, int ev) {
    EXPECT_EQ(fd, p.a);
    EXPECT_EQ(ev, kReadable);
    char buf[64];
    ssize_t n = ::read(fd, buf, sizeof buf);
    if (n > 0) got.append(buf, static_cast<size_t>(n));
  }));
  EXPECT_EQ(loop.mask(p.a), kReadable);
  EXPECT_EQ(loop.process_events(0), 0) << "nothing to read yet";
  ASSERT_EQ(::write(p.b, "hi", 2), 2);
  EXPECT_EQ(loop.process_events(1000), 1);
  EXPECT_EQ(got, "hi");
}

TEST(EventLoop, WritableAtOnceForAnEmptySocket) {
  SystemClock clock;
  EventLoop loop(clock);
  Pair p;
  int calls = 0;
  ASSERT_TRUE(loop.add(p.a, kWritable, [&](int, int ev) {
    EXPECT_EQ(ev, kWritable);
    ++calls;
  }));
  loop.process_events(1000);
  EXPECT_EQ(calls, 1);
}

TEST(EventLoop, ReadAndWriteHandlersAreSeparate) {
  SystemClock clock;
  EventLoop loop(clock);
  Pair p;
  std::string log;
  ASSERT_TRUE(loop.add(p.a, kReadable, [&](int fd, int) {
    char c;
    while (::read(fd, &c, 1) == 1) {
    }
    log += "r";
  }));
  ASSERT_TRUE(loop.add(p.a, kWritable, [&](int, int) { log += "w"; }));
  EXPECT_EQ(loop.mask(p.a), kReadable | kWritable);
  ASSERT_EQ(::write(p.b, "x", 1), 1);
  loop.process_events(1000);
  EXPECT_EQ(log, "rw") << "readable first, then writable";
  loop.remove(p.a, kWritable);
  EXPECT_EQ(loop.mask(p.a), kReadable);
  ASSERT_EQ(::write(p.b, "x", 1), 1);
  loop.process_events(1000);
  EXPECT_EQ(log, "rwr");
}

TEST(EventLoop, RemoveEverything) {
  SystemClock clock;
  EventLoop loop(clock);
  Pair p;
  int calls = 0;
  loop.add(p.a, kReadable | kWritable, [&](int, int) { ++calls; });
  loop.remove(p.a, kReadable | kWritable);
  EXPECT_EQ(loop.mask(p.a), 0);
  ASSERT_EQ(::write(p.b, "x", 1), 1);
  loop.process_events(50);
  EXPECT_EQ(calls, 0);
  loop.remove(p.a, kReadable);  // not watched: harmless
}

// The pitfall: the kernel has already reported both fds as ready. The first handler
// (whichever it is) stops watching the other one, maybe closes it, maybe frees the
// object behind it. The other handler must not run.
TEST(EventLoop, HandlerRemovingAnotherReadyFd) {
  SystemClock clock;
  EventLoop loop(clock);
  Pair p, q;
  int ran = 0;
  auto handler = [&](int fd, int) {
    ++ran;
    loop.remove(fd == p.a ? q.a : p.a, kReadable);
  };
  loop.add(p.a, kReadable, handler);
  loop.add(q.a, kReadable, handler);
  ASSERT_EQ(::write(p.b, "x", 1), 1);
  ASSERT_EQ(::write(q.b, "x", 1), 1);
  std::this_thread::sleep_for(std::chrono::milliseconds(20));
  loop.process_events(1000);
  EXPECT_EQ(ran, 1);
}

// A handler may remove itself (a client being closed) or replace itself.
TEST(EventLoop, HandlerRemovingItself) {
  SystemClock clock;
  EventLoop loop(clock);
  Pair p;
  int ran = 0;
  loop.add(p.a, kReadable, [&](int fd, int) {
    ++ran;
    loop.remove(fd, kReadable);
  });
  ASSERT_EQ(::write(p.b, "xyz", 3), 3);
  loop.process_events(1000);
  loop.process_events(50);
  EXPECT_EQ(ran, 1);
}

TEST(EventLoop, AddingAClosedFdFails) {
  SystemClock clock;
  EventLoop loop(clock);
  int fds[2];
  ASSERT_EQ(::socketpair(AF_UNIX, SOCK_STREAM, 0, fds), 0);
  ::close(fds[0]);
  ::close(fds[1]);
  EXPECT_FALSE(loop.add(fds[0], kReadable, [](int, int) {}));
  EXPECT_EQ(loop.mask(fds[0]), 0);
}

TEST(EventLoop, WaitsForTheNextTimer) {
  SystemClock clock;
  EventLoop loop(clock);
  bool fired = false;
  loop.timers().add(clock.now_ms() + 40, [&](TimerId) { fired = true; return kNoMore; });
  auto t0 = std::chrono::steady_clock::now();
  EXPECT_EQ(loop.process_events(-1), 1) << "with no fd events, -1 would wait forever; the timer limits it";
  EXPECT_TRUE(fired);
  EXPECT_GE(elapsed_ms(t0), 30);
  EXPECT_LT(elapsed_ms(t0), 1000);
}

TEST(EventLoop, OverdueTimerMeansNoWaiting) {
  FakeClock clock(1000);
  EventLoop loop(clock);
  int fired = 0;
  loop.timers().add(500, [&](TimerId) { ++fired; return kNoMore; });
  auto t0 = std::chrono::steady_clock::now();
  loop.process_events(-1);
  EXPECT_EQ(fired, 1);
  EXPECT_LT(elapsed_ms(t0), 100);
}

TEST(EventLoop, BeforeSleepRunsEveryIteration) {
  SystemClock clock;
  EventLoop loop(clock);
  int calls = 0;
  loop.set_before_sleep([&] { ++calls; });
  loop.process_events(0);
  loop.process_events(0);
  EXPECT_EQ(calls, 2);
}

TEST(EventLoop, StopFromAHandler) {
  SystemClock clock;
  EventLoop loop(clock);
  int ticks = 0;
  loop.timers().add(clock.now_ms(), [&](TimerId) {
    if (++ticks == 3) loop.stop();
    return 1;
  });
  loop.run();
  EXPECT_EQ(ticks, 3);
}

// The pitfall: the loop is asleep in kevent() with nothing to wake it. stop() from
// another thread must wake it (Redis uses a pipe for this).
TEST(EventLoop, StopFromAnotherThreadWakesTheLoop) {
  SystemClock clock;
  EventLoop loop(clock);
  auto t0 = std::chrono::steady_clock::now();
  std::thread stopper([&] {
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    loop.stop();
  });
  loop.run();  // no fds, no timers: only stop() can end this
  stopper.join();
  EXPECT_LT(elapsed_ms(t0), 2000);
}

}  // namespace
}  // namespace rd
