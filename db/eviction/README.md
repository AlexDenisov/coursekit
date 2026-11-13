# db/eviction: maxmemory and approximate LRU

> **Summary.** With `maxmemory` set, Redis is a cache. Before a command that
> can add data, it evicts keys until memory is back under the limit,
> preferring the least recently used. True LRU needs a linked list updated on
> every read. Redis instead stores a 24-bit timestamp per key, samples a few
> keys, and keeps a small pool of the best candidates seen so far. It's nearly
> as good, and free on reads. `server/lean_redis` runs on it. The surprising
> part: 5 random samples plus a memory of 16 beat 5 random samples by a wide
> margin.
>
> **Before:** `db/keyspace` (the `lru` field), `db/expire` (the volatile-*
> policies). **Next:** `server/lean_redis`.

## Policies

| Policy | Evicts |
|---|---|
| `noeviction` | nothing: writes fail with `OOM command not allowed when used memory > 'maxmemory'.` |
| `allkeys-lru` / `volatile-lru` | the least recently used key (among keys with a TTL) |
| `allkeys-random` / `volatile-random` | any key (with a TTL) |
| `volatile-ttl` | the key closest to expiring |

Only commands that can grow memory are checked (`is_denyoom`): `GET` and `DEL`
always run, even when every write is refused (recorded from Redis).

## The sampling pool

```
repeat while used_memory() > maxmemory:
    sample 5 random keys, score each (idle seconds, or how soon it expires)
    merge them into the pool: the 16 best candidates so far, best last
    evict the best candidate that still exists
```

Without the pool, each eviction only sees 5 keys. With it, a very old key seen
three evictions ago is still remembered. `Eviction.ThePoolRemembersGoodCandidates`
and `Eviction.ApproximateLruIsCloseToExact` measure the quality. A candidate in
the pool may have been deleted or rewritten since it was sampled, so check
before evicting.

## The pitfall: a clock that wraps

`lru` holds seconds modulo 2^24 (about 194 days). After a wrap, "now" is
numerically smaller than an old key's stamp, and naive subtraction makes that
key look brand new (`IdleSeconds.AcrossTheWrap`).

## Optional: LFU

Redis can also evict the *least frequently* used key. Its 8-bit counter is
logarithmic: each hit increments it with probability `1 / ((c - 5) * 10 + 1)`,
so 100 hits reach about 10, a million saturate at 255. It decays by one per
minute of idleness.

## API

| Function | Contract |
|---|---|
| `Evictor(ks, rng, config, used_memory)` / `make_room()` | evict until under the limit; false if impossible |
| `dispatch_with_eviction(table, evictor, args, reply)` | the OOM gate around a command |
| `idle_seconds(lru, now)` | wrap-aware |
| `parse_policy`, `is_denyoom` | given |
| `lfu_log_incr`, `lfu_decay` (optional) | LFU counters |

## Task

    bazel test //db/eviction:eviction_test
    bazel test //db/eviction:optional_test
