# persist/aof: Append-only file

> **Summary.** An RDB snapshot every few minutes loses minutes of writes in
> a crash. The append-only file loses at most a second: every write command is
> appended to a log, as RESP, and replayed at startup. Your parser reads it
> back. `persist/aof_rewrite` compacts it, and `server/durable_redis` runs on
> it. The surprising part: logging `EXPIRE k 100` as-is would be a bug.
>
> **Before:** `resp/parser`, `resp/encoder`, `cmd/dispatch`, `db/keyspace`.
> **Next:** `persist/aof_rewrite`, `server/durable_redis`.

## What gets logged

Only commands that changed something. The keyspace's `dirty()` counter tells
you: compare it before and after the command. Real Redis, recorded by
`tools/aof_reference.py`:

```
SET k v              logged
GET k                -            (a read)
DEL nothere          -            (changed nothing)
SET t v EX 100       SET t v PXAT 1794519193533
EXPIRE k 100         PEXPIREAT k 1794519193542
```

## The pitfall: relative time

Replaying `SET t v EX 100` tomorrow would give `t` 100 more seconds from
*tomorrow*. Redis rewrites every relative TTL into an absolute one before
logging (`log_form`), so the replay restores exactly the same deadline
(`AofTest.ReplayRestoresTheData` replays a minute later and checks).

## When the bytes hit the disk

`append` only queues. `flush` writes, and Redis calls it in `beforeSleep`,
*before* sending replies. So a client that saw `+OK` knows the write is at
least in the OS. Then `fsync` decides how durable:

| `appendfsync` | fsync | lose in a power cut |
|---|---|---|
| `always` | after every flush | nothing acknowledged |
| `everysec` | from the cron, at most once a second | about one second |
| `no` | never (the OS decides) | up to ~30 s |

## Replay and torn tails

At startup, parse requests from the file and run them, discarding replies. A
crash can leave half a request at the end. That's expected: Redis replays what
is complete and cuts the file back to the last complete request, so new
appends don't land after garbage (`AofTest.TruncatedTailIsDroppedAndCut`). A
protocol error in the *middle* is real corruption and stops the load.

## API

| Function | Contract |
|---|---|
| `log_form(args, now_ms)` | Redis's propagation rewrites |
| `Aof::open/reopen/append/flush/cron` | the log; fsync per policy |
| `Aof::set_capture(buf)` | for `persist/aof_rewrite` |
| `dispatch_logged(table, ks, aof, args, reply)` | run, and log if it wrote |
| `replay_aof(data, table)`, `load_aof_file(path, table)` | startup |

## Task

    bazel test //persist/aof:aof_test
