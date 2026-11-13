# persist/aof_rewrite: AOF rewrite

> **Summary.** An AOF grows forever: 2000 `INCR n` take 2000 log entries for
> one key holding "2000". A rewrite replaces the history with the shortest log
> that rebuilds the *current* state, computed from a forked snapshot while
> the server keeps running. `server/durable_redis` triggers it with
> `BGREWRITEAOF`. The surprising part: writes that arrive during the rewrite
> must be logged twice.
>
> **Before:** `persist/aof`, `persist/bgsave` (the same fork machinery).
> **Next:** `server/durable_redis`.

## The new log

`rewrite_commands` walks the keyspace and emits one command per key, plus
`PEXPIREAT` for keys with a TTL:

```
SET n 2000
RPUSH l e0 ... e63          aggregates in batches of 64 elements, so no single
RPUSH l e64 ... e127        command is gigantic (Redis's AOF_REWRITE_ITEMS_PER_CMD)
HSET h f0 v0 ... f63 v63
ZADD z 0.5 z0 1.5 z1 ...
PEXPIREAT ttl 1800000050000
```

## The race, and the fix

```
t0  fork()                         child: snapshot at t0, writes appendonly.aof.rewrite
t1  SET during x                   → parent logs it in the OLD aof... and the child
                                     can't see it: the new file would lose it
t2  child exits
```

So from the fork on, the parent *captures* every request it appends
(`Aof::set_capture`). When the child is done: flush the old AOF, append the
captured requests to the new file, `rename` it over the old one, and
`reopen` the AOF so new appends go to the new file. The process is
single-threaded, so nothing can sneak in between the flush and the rename.
`Rewrite.BackgroundRewrite` writes during the rewrite and checks that the
replayed log still has it.

(Redis 7 replaced this with a manifest of base and incremental files, which
avoids holding the capture buffer in memory. The idea is the same.)

## API

| Function | Contract |
|---|---|
| `rewrite_commands(ks)` | the compact log, batched by 64 |
| `AofRewriter::start/poll/in_progress/last_status` | fork, capture, swap |
| `register_rewrite_command(table, rw)` | BGREWRITEAOF |

## Task

    bazel test //persist/aof_rewrite:aof_rewrite_test
