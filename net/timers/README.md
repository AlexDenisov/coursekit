# net/timers: Timer events

> **Summary.** Besides sockets, Redis's event loop runs *time events*:
> `serverCron` ten times a second (active expiry, resizing dicts, reaping
> BGSAVE children), and timeouts for blocked clients. `net/event_loop` uses
> this to decide how long it may sleep and what to run when it wakes.
> `db/expire`, `persist/bgsave` and `msg/blocking` all schedule timers. The
> surprising part: deleting from a heap is easiest when you don't.
>
> **Before:** nothing. **Next:** `net/event_loop`, `msg/blocking`.

## The interface Redis uses

A timer has a deadline and a callback. The callback's return value decides what
happens next:

```
id = add(now + 100, cron)      cron returns 100  ->  runs again 100 ms after it ran
id = add(now + 5000, timeout)  timeout returns kNoMore  ->  gone
```

The loop asks `ms_until_next(now)`, sleeps in `kevent` for at most that long,
then calls `run_due(now)`.

## A heap with lazy deletion

Keep a min-heap of `(when, id)` and a map `id -> (when, callback)` of the live
timers.

- **add**: insert into both. O(log n).
- **cancel**: erase from the map only. The heap entry becomes *stale*.
- **reschedule**: update the map's `when` and push a new entry. The old entry
  is stale too: its `when` no longer matches.
- **next_deadline / run_due**: pop stale entries whenever they reach the top.

Each entry is pushed once and popped once, so everything stays amortised
O(log n). A sorted list would make `cancel` O(n), and 200 000 timers would take
forever (`Timers.ManyTimersAreCheap`).

## The pitfalls: callbacks that change the timers

`run_due` calls code that can call back into `Timers`:

- A callback cancels another timer that is also due: that timer must not run
  (`Timers.CallbackCancelsAnotherDueTimer`). Check the map right before each
  call, not once up front.
- A callback adds a timer due "now": Redis doesn't run it in the same pass,
  otherwise a callback that keeps adding itself would never return
  (`Timers.TimerAddedByACallbackWaitsForTheNextCall`). Remember the highest id
  that existed when `run_due` started.
- A callback returns 0, "again right away": once per call, same reason
  (`Timers.ZeroDelayRunsOncePerCall`).
- Don't hold a reference into the map across the call. The callback may insert
  and rehash it. Move the callback out, call it, put it back if it's still
  live.

## API

| Function | Contract |
|---|---|
| `add(when_ms, cb)` | returns the id (1, 2, ...) |
| `cancel(id)` | false if not pending |
| `size()` | live timers |
| `next_deadline()` | earliest live deadline |
| `ms_until_next(now)` | 0 if due, -1 if none |
| `run_due(now)` | due timers by (when, id); returns how many ran |

## Task

    bazel test //net/timers:timers_test
