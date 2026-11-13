# server/lean_redis: ★ Lean Redis

> **Summary.** The Internals milestone: your Mini Redis becomes a cache
> server. It gets sorted sets (a leaderboard), a memory limit with approximate
> LRU eviction, and `CONFIG SET maxmemory` like the real thing. Then it goes
> head-to-head with `redis-server` under `redis-benchmark`, aiming for at least
> half its throughput. The surprising part: measuring memory is the expensive
> step, more than evicting.
>
> **Before:** `server/mini_redis`, `db/zsets`, `db/eviction` (and
> `db/encodings` as background). **Next:** the Durability and Messaging
> tracks.

## What to write

In `LeanRedis`'s constructor:

1. Register the zset commands on `base_.table()`.
2. Install a runner on every new client (`base_.server().set_new_client_hook`).
   Before a denyoom command, ask the `Evictor` to make room. If it can't, reply
   `kOomError`; otherwise dispatch as usual.

## The pitfall: measuring is expensive

Redis keeps an atomic counter of bytes allocated (`zmalloc`), so asking "how
much memory?" is free. You don't have that. `heap_bytes_in_use()` asks the
macOS allocator, which takes about 50 µs, and at a million commands a second
that alone would be 50 seconds of work per second. So check *at most once per
millisecond* and reuse the last answer in between. The limit becomes slightly
soft (a millisecond's worth of writes can overshoot), and throughput stays
where it was. `LeanRedisTest.EvictsToStayUnderMaxmemory` allows 16 MB of slack
over a 32 MB budget.

## Try it

```sh
bazel run -c opt //server/lean_redis -- 6380 50000000 allkeys-lru
redis-benchmark -p 6380 -t set -n 1000000 -r 10000000 -d 1000 -q   # 1 GB into 50 MB
redis-cli -p 6380 DBSIZE                                            # a fraction stayed
redis-cli -p 6380 ZADD board 100 ann 250 bob
redis-cli -p 6380 ZRANGE board 0 -1 REV WITHSCORES
```

## The comparison

`versus_redis_test` runs the same `redis-benchmark` workload (SET, GET, INCR,
LPUSH, ZADD; 50 clients, pipelined) against your server and a real
`redis-server`, and prints both. The reference solution reaches 0.6–1.0×
Redis on an M-series Mac. If you're below 0.5×, profile: common culprits are
copying arguments, `std::function` in the dispatch path, reading 1 byte at a
time, or a `kWritable` registration that never goes away.

## API

| Function | Contract |
|---|---|
| `LeanRedis(clock, options)` | MiniRedis + zsets + CONFIG + eviction gate |
| `heap_bytes_in_use()` | given; ~50 µs |
| `register_config_command()` | given: CONFIG GET/SET maxmemory, maxmemory-policy |

## Task

    bazel test //server/lean_redis:lean_redis_test --test_output=all
    bazel test -c opt //server/lean_redis:versus_redis_test --test_output=all
