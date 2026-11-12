# ds/dict: Dict with incremental rehash

> **Summary.** Redis's keyspace, every hash, every set, the expires table and
> the command table are all one data structure: `dict.c`. It's a chained hash
> table with a twist. When it grows or shrinks, it migrates entries a bucket
> at a time, spread over the following operations, so no single command pays
> for copying millions of entries. `db/keyspace`, `db/hashes`, `db/sets` and
> `db/zsets` store their data in yours, and `ds/dict_scan` iterates it. The
> surprising part: for a while, every key lives in one of *two* tables.
>
> **Before:** nothing. **Next:** `ds/dict_scan`, `db/keyspace`.

## Two tables

```
not rehashing:   table 0 [4 buckets] ──> chains        table 1: none
growing:         table 0 [4]  migrated: buckets 0..rehash_index-1 (now empty)
                 table 1 [8]  new keys go here; migrated chains land here
done:            table 1 becomes table 0
```

A `std::unordered_map` rehashes all at once. With 10 million keys, that means
one `SET` that takes a second, and on a single-threaded server every client
waits for it. Redis spreads the cost: each `find`, `insert` and `erase` first
moves one bucket (`rehash_step(1)`), and the server's cron moves more when
it's idle (`server/mini_redis`). While both tables exist, a lookup checks
table 0, then table 1. An insert goes straight into table 1, so table 0 only
ever shrinks.

## The numbers (all tested)

| Event | Rule |
|---|---|
| first insert | allocate 4 buckets |
| insert when `size() >= buckets` | rehash into the next power of two `>= size() + 1` (so 4 → 8 → 16 ...) |
| erase leaving `size() * 8 <= buckets` (and buckets > 4) | rehash into `max(4, next power of two >= size())` |
| any non-const op while rehashing | `rehash_step(1)` first |
| `rehash_step(n)` | move up to n non-empty buckets; give up after 10n empty ones |

The 10n limit matters. A shrinking table can have long runs of empty buckets,
and a "step" that scans 100 000 of them is no longer a small step.

## The pitfalls

- **Forgetting table 1.** Lookups, erases and iteration must cover both tables
  while rehashing (`Dict.LookupsDuringRehashSeeBothTables`).
- **Rehashing all at once.** That's correct but defeats the point, and
  `Dict.RehashingIsIncremental` checks that 100 lookups into a fresh
  65 536 → 131 072 rehash don't finish it.
- **Iterating while rehashing.** `for_each` must not do rehash steps: entries
  would move behind or ahead of the iteration and be visited twice or not at
  all. (Redis's "safe iterator" pauses rehashing for exactly this reason.)

## A template

`Dict<V>` is a class template, so its code lives in a header: write it in
`dict_impl.h`, which `dict.h` includes at the end. `Entry` chains use
`std::unique_ptr<Entry> next`. To move a whole chain during rehash, relink
the nodes; don't copy keys or values.

## API

| Function | Contract |
|---|---|
| `find(key)` | value or nullptr (non-const: one rehash step first) |
| `insert(key, value)` | `{value*, added}`; never overwrites |
| `get_or_insert(key)` | default-construct if missing |
| `erase(key)`, `clear()`, `size()` | |
| `for_each(fn)` | every entry once, no rehashing meanwhile |
| `random_key(rng)` | a random key (RANDOMKEY, expiry and eviction sampling) |
| `is_rehashing()`, `rehash_step(n)`, `bucket_count(t)` | the rehash machinery |
| `for_each_in_bucket(t, i, fn)`, `bucket_of(key, t)` | for `ds/dict_scan` |

## Task

    bazel test //ds/dict:dict_test
