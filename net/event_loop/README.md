# net/event_loop: kqueue event loop

> **Summary.** This is the heart of a single-threaded server. Ask the kernel
> which of thousands of sockets are ready, run their handlers, run due timers,
> repeat. It's Redis's `ae.c` (with `ae_kqueue.c` on macOS), in about 100
> lines. `server/event_server` and everything after run on it. The surprising
> part: most bugs come from handlers changing the loop while it's iterating.
>
> **Before:** `net/timers`, `net/nonblocking`. **Next:** `net/client`,
> `server/event_server`.

## kqueue in three calls

```
kq = kqueue()
kevent(kq, changes = [{fd, EVFILT_READ, EV_ADD}], ...)       start watching
n = kevent(kq, nullptr, 0, events, 128, &timeout)            wait for readiness
```

Each fd has up to two filters, `EVFILT_READ` and `EVFILT_WRITE`, each with its
own handler (Redis's `rfileProc` / `wfileProc`). Events are *level-triggered*:
a socket with unread data stays readable, so the loop reports it again next
time. That's what lets `net/client` read just 16 KB per turn.

## One iteration

```
process_events(max_wait):
    before_sleep()                              flush replies, ... (Redis's beforeSleep)
    timeout = min(time until next timer, max_wait)
    events = kevent(..., timeout)
    for each event: if still watched, run its handler
    timers.run_due(now)
```

The timeout is how timers and sockets share one thread. With a cron every
100 ms and no traffic, `kevent` sleeps 100 ms and the cron runs on time.

## The pitfalls

1. **Stale events.** `kevent` returned events for fds A and B. A's handler
   closes client B: it removes B's events and frees B. B's event is still in
   your array. Running its handler now is a use-after-free in a real server.
   Before each handler, look the fd up again and check that the event is still
   watched (`EventLoop.HandlerRemovingAnotherReadyFd`).
2. **Handlers that remove themselves.** Copy the `std::function` before calling
   it. Removing it from the map while it's executing destroys the closure you're
   running (`EventLoop.HandlerRemovingItself`).
3. **Waking a sleeping loop.** `run()` may be blocked in `kevent` with no
   timeout. `stop()` from another thread sets a flag that nobody will look at.
   The classic fix is a self-pipe: the loop watches the read end of a pipe, and
   `stop()` writes a byte to it (`EventLoop.StopFromAnotherThreadWakesTheLoop`).
   Drain it in the handler, and don't count it as a user event.

## API

| Function | Contract |
|---|---|
| `EventLoop(clock)` | kqueue + wake pipe |
| `add(fd, mask, handler)` | watch `kReadable` / `kWritable`; one handler per event |
| `remove(fd, mask)` | stop watching; immediate |
| `mask(fd)` | what's watched |
| `timers()` | the time events (`net/timers`) |
| `set_before_sleep(fn)` | runs at the start of each iteration |
| `process_events(max_wait_ms)` | one iteration; returns events handled + timers run |
| `run()`, `stop()` | loop until stopped; `stop` is thread-safe |

## Task

    bazel test //net/event_loop:event_loop_test

(kqueue is macOS/BSD; on Linux you'd write the same thing on `epoll`. That's a
good side exercise once this works.)
