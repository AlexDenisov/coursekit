# server/mini_redis: ★ Mini Redis

> **Summary.** The main goal of the course. Everything from the Protocol,
> Event loop and Keyspace tracks, wired into one server: strings with TTLs,
> lists, hashes, sets, KEYS/SCAN, and a cron that expires keys and finishes
> rehashes in the background. It passes a compatibility suite of 465 commands
> whose expected replies were recorded byte for byte from Redis 8.10, and the
> real `redis-cli` and `redis-benchmark` work against it. The surprising part:
> the milestone itself is about 30 lines. Everything else is code you already
> wrote.
>
> **Before:** `server/event_server`, `db/expire`, `db/lists`, `db/hashes`,
> `db/sets`, `db/generic`. **Next:** the advanced tracks, `server/lean_redis`,
> `server/durable_redis`, `server/messaging_redis`.

## What to write

`MiniRedis`'s constructor registers every command family on one
`CommandTable`, all sharing one `Keyspace`, then schedules the cron as a
timer on the event loop:

```cpp
register_connection_commands(table_);
register_keyspace_commands(table_, ks_);
register_expire_commands(table_, ks_);
...                                        // lists, hashes, sets (+ optional), generic
server_.loop().timers().add(now + 100, [this](TimerId) { cron(); return 100; });
```

`cron()` is Redis's `databasesCron` in miniature:

1. `active_expire_cycle`: keys that expired and that nobody reads again get
   deleted anyway (`MiniRedisTest.CronExpiresKeysNobodyReads`).
2. `rehash_step(100)` on both dicts if they're rehashing. Without traffic,
   nothing else would advance a rehash, and the old table would hold its memory
   indefinitely (`MiniRedisCron.FinishesRehashingWithoutTraffic`).

## The compatibility suite

`compat_cases.inc` replays every case file from `tools/cases/` (dispatch,
strings, keyspace, expire, lists, hashes, sets, generic) over a real TCP
connection, one group per connection, after a `FLUSHALL`. Order-free replies
(`SMEMBERS`, `HGETALL`, `KEYS`, ...) are compared as multisets. The test prints
how many steps match, so you can watch the number climb. Optional parts aren't
in the suite.

Regenerate it after adding cases:

```sh
python3 tools/redis_reference.py --inc tools/cases/{dispatch,strings,keyspace,expire,lists,hashes,sets,generic}.txt \
    > server/mini_redis/compat_cases.inc
```

## Try it

```sh
bazel run -c opt //server/mini_redis -- 6380
redis-cli -p 6380
127.0.0.1:6380> RPUSH queue job1 job2
127.0.0.1:6380> HSET user:1 name ann visits 1
127.0.0.1:6380> SET session abc EX 30
127.0.0.1:6380> TTL session
redis-cli -p 6380 --scan --pattern 'user:*'
redis-benchmark -p 6380 -c 50 -n 100000 -P 16 -t set,get,lpush,lpop,sadd,hset -q
```

## API

| Function | Contract |
|---|---|
| `MiniRedis(clock, options)` | registers everything, schedules the cron |
| `cron()` | active expiry + incremental rehashing |
| `listen`, `run`, `stop`, `keyspace()`, `table()`, `server()` | given |
| `main.cc` | given |

## Task

    bazel test //server/mini_redis:mini_redis_test --test_output=all
    bazel test -c opt //server/mini_redis:real_clients_test --test_output=all
