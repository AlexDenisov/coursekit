# db/generic: Generic key commands

> **Summary.** Commands about keys, whatever their type: `KEYS`, `SCAN`,
> `RENAME`, `DBSIZE`, `FLUSHDB`, `RANDOMKEY`. They combine your glob matcher,
> your dict's scan cursor and its random sampling. `server/mini_redis` exposes
> them, and `redis-cli --scan` is built on SCAN. The surprising part: `KEYS`
> and `SCAN` return the same keys, but one of them can take your server down.
>
> **Before:** `db/keyspace`, `db/glob`, `ds/dict_scan` (and `db/expire` for
> the TTL in RENAME). **Next:** `server/mini_redis`.

## KEYS vs SCAN

`KEYS user:*` walks every entry and runs `glob_match` on each, in one command.
With 50 million keys that's seconds during which no other client is served,
which is why it's banned in production configs (`rename-command KEYS ""`).

`SCAN cursor MATCH user:* COUNT 100` does a slice: call `dict_scan` until
about COUNT entries were visited (at most 10 × COUNT calls, for empty
stretches), filter, and return the next cursor. The client loops until the
cursor is `0` again, and other clients get served between the slices.
`ds/dict_scan`'s guarantee carries over: every key that exists throughout is
returned at least once (`Generic.ScanSurvivesWritesBetweenCalls`).

Filtering happens *after* the walk, so a `SCAN` with a selective `MATCH` often
returns zero keys with a non-zero cursor. That's normal: keep going.

## The details (all recorded from Redis)

- `KEYS *` skips the matcher entirely, so the empty key matches. With the
  matcher alone it wouldn't (see `db/glob`).
- `SCAN` cursor not a number: `ERR invalid cursor`. `COUNT 0`: `ERR syntax
  error`. `TYPE nosuchtype` isn't an error, just nothing matches.
- `RENAME` moves the value *and its TTL*. It also drops whatever TTL `dst` had
  (`Generic.RenameDropsTheTargetsOldTtl`). `RENAME k k` is fine.
- Expired keys are invisible to KEYS, SCAN and RANDOMKEY, but `DBSIZE` counts
  what's stored, expired or not. That's Redis's behaviour, and cheap.

## API

| Function | Contract |
|---|---|
| `register_generic_commands(table, ks, rng)` | KEYS SCAN RENAME RENAMENX DBSIZE FLUSHDB FLUSHALL RANDOMKEY; see `generic.h` |

## Task

    bazel test //db/generic:generic_test
