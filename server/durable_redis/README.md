# server/durable_redis: ★ Durable Redis

> **Summary.** The Durability milestone: your Mini Redis stops being a
> volatile cache. It logs every write to an AOF, snapshots with BGSAVE,
> compacts its log with BGREWRITEAOF, loads everything back at startup, and
> can follow a real Redis primary with REPLICAOF. The tests kill it with
> SIGKILL mid-flight and check what comes back, and in the other direction
> have real Redis load your `dump.rdb`. The surprising part: "durable"
> mostly means being careful about *order*: log before replying, AOF before
> RDB, rename after fsync.
>
> **Before:** `server/mini_redis`, `persist/bgsave`, `persist/rdb_reader`,
> `persist/aof_rewrite`, `repl/replica`. **Next:** the Messaging track.

## What to write

- **The constructor.** Register SAVE/BGSAVE/LASTSAVE, BGREWRITEAOF and
  REPLICAOF. Install a runner on every client that, with `appendonly`, logs
  writes (`dispatch_logged`) and flushes the AOF right away, before the reply
  can reach the client. Add a 100 ms timer for `aof.cron()` (everysec fsync).
- **`load()`.** If there's an AOF, replay it; otherwise load `dump.rdb`. The
  AOF wins when both exist, because it's newer
  (`Durable.AofIsPreferredOverRdb`). A file that exists but won't load is
  fatal: starting empty would silently throw the data away
  (`Durable.CorruptRdbRefusesToStart`).

## kill -9

`Durable.KillDashNine` forks a real server process, writes 1000 keys,
waits 1.5 s, and `SIGKILL`s it: no destructors, no flush, nothing. A fresh
server on the same directory must have all of them. With
`appendfsync everysec`, anything older than about a second is guaranteed to be
on disk; `always` closes even that gap at the cost of an fsync per write.

## Try it

```sh
mkdir -p /tmp/rd
bazel run -c opt //server/durable_redis -- 6380 /tmp/rd &
redis-cli -p 6380 SET k v
kill -9 %1
bazel run -c opt //server/durable_redis -- 6380 /tmp/rd     # "1 keys loaded"
redis-server --port 6390 --save "" &                         # a real primary
redis-cli -p 6380 REPLICAOF 127.0.0.1 6390
redis-cli -p 6390 SET from-primary yes; redis-cli -p 6380 GET from-primary
```

## API

| Function | Contract |
|---|---|
| `DurableRedis(clock, options)` | wiring: commands, AOF runner, cron |
| `load()` | AOF, else RDB; then open the AOF |
| `listen`, `run`, `stop`, `base()`, `replica()`, `aof()` | given |

## Task

    bazel test //server/durable_redis:durable_redis_test
    bazel test //server/durable_redis:real_redis_test --test_output=all   # needs redis-server
