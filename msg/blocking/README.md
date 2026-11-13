# msg/blocking: Blocking pops

> **Summary.** `BLPOP jobs 0` waits until there's a job. That one command
> turns a Redis list into a work queue: producers `LPUSH`, workers `BRPOP`,
> and nobody polls. On a single-threaded server, "waiting" can't mean
> blocking a thread. The client is parked in a data structure, and another
> client's push wakes it. `server/messaging_redis` runs workers on it. The
> surprising part: the waiter is served *after* the pushing command
> finishes, not inside it.
>
> **Before:** `db/lists`, `net/timers`, `net/client` (`block`/`unblock`).
> **Next:** `server/messaging_redis`.

## What Redis does (recorded with three connections)

```
A: BLPOP q 0           (nothing: A is parked, no reply yet)
C: BLPOP q 0           (parked behind A)
B: RPUSH q 1 2 3       B ← :3        ← the length right after the push
                       A ← [q, 1]    ← then the waiters, oldest first
                       C ← [q, 2]
B: LRANGE q 0 -1       B ← [3]
```

`RPUSH` replies `:3`, not `:1`. The push completes, *then* `serve_ready()`
hands elements to waiters. Serving inside `RPUSH` would make its reply depend
on who happens to be waiting, and would run one client's pop in the middle of
another client's command.

## The machinery

- `waiting_`: session → its keys, direction and timer.
- `by_key_`: key → waiting sessions, oldest first.
- `pop` either answers at once, or parks the session on every key and tells
  the server to stop running that client's further requests (`on_block`).
  Its next request must wait until BLPOP has an answer.
- `serve_ready()`, after every command: any key with waiters and elements
  serves them. Serving removes the waiter from *all* its keys
  (`BlockingTest.ServedOnceAcrossKeys`) and cancels its timeout.
- The timeout is a timer that pushes `*-1` and unblocks.
- Inside `MULTI`/`EXEC` (and without a session), never block: reply `*-1` at
  once, as Redis does.

## API

| Function | Contract |
|---|---|
| `Blocking(ks, timers, clock, on_block, on_unblock)` | |
| `pop(s, args, left, may_block, reply)` | true: replied; false: parked |
| `serve_ready()` | after every command |
| `blocked(s)`, `blocked_count()`, `forget(s)` | |
| `register_blocking_commands(table, blocking, may_block)` | BLPOP, BRPOP |

## Task

    bazel test //msg/blocking:blocking_test
