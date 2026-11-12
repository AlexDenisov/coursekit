# db/expire: Expiry

> **Summary.** `db/keyspace` stores TTLs and deletes expired keys lazily, when
> they're next touched. This exercise adds the commands (EXPIRE, TTL,
> PERSIST, ...) and the other half of the mechanism: an *active* cycle that
> samples keys with a TTL ten times a second and deletes the expired ones, so
> keys nobody reads again don't leak memory forever. `server/mini_redis` runs
> the cycle from a timer. The surprising part: Redis never scans all TTLs. A
> sample of 20 is enough to tell when to stop.
>
> **Before:** `db/keyspace`. **Next:** `server/mini_redis`, `db/eviction`.

## The commands

```
EXPIRE k 100          TTL k  -> 100          PTTL k -> 100000
EXPIRE k 50 GT        :0   (50 s isn't later than 100 s)
EXPIRE k 10 LT        :1
PERSIST k             :1   TTL k -> -1 (no TTL)        TTL nokey -> -2
EXPIRE k 0            :1   and k is gone: a time in the past deletes
```

`NX` sets only without a TTL, `XX` only with one. For `GT`/`LT`, "no TTL"
means infinite: `GT` never applies, `LT` always does. `TTL` rounds to the
nearest second, `(ms + 500) / 1000`. Every error message comes from Redis 8;
see `expire.h`.

## Active expiry

Lazy expiry alone keeps every key that expired and was never read. Scanning
all TTLs ten times a second is O(n) per tick. Redis's compromise, run from
`serverCron`:

```
repeat (up to a budget):
    sample 20 random keys that have a TTL
    delete the ones that have expired
    if at most 10% of the sample had expired: stop
```

The sample is a cheap estimate of how stale the whole set is. If 15 out of 20
had expired, most of the TTL table is garbage, so keep going. If 1 out of 20,
the garbage is down to a few percent, so stop and let lazy expiry and later
ticks handle the rest. The cost per tick is bounded and proportional to the
garbage, not to the number of keys.

(Redis 6+ samples by walking the expires table with a scan cursor rather than
by random picks, and adds a CPU-time budget. The stopping rule is the same.)

## The pitfalls

- `random_key` returns a pointer into the dict. Deleting the key frees it, so
  copy the string first.
- Delete through the keyspace's lazy path (`find` on an expired key), not with
  `erase`. Expiry isn't a write by a command: it must not bump `dirty()`
  (`ActiveExpire.DeletionsAreNotCommandWrites`).
- Never touch a key whose TTL hasn't passed (`ActiveExpire.NeverDeletesLiveKeys`).

## API

| Function | Contract |
|---|---|
| `register_expire_commands(table, ks)` | EXPIRE PEXPIRE EXPIREAT PEXPIREAT TTL PTTL EXPIRETIME PEXPIRETIME PERSIST |
| `active_expire_cycle(ks, rng, max_loops)` | the sampling loop; returns `{loops, sampled, expired}` |

## Task

    bazel test //db/expire:expire_test
