# db/keyspace: Typed keyspace

> **Summary.** This is the real database: one dict from key to a typed value,
> a second dict from key to expiry time, and an LRU clock per key. The string
> commands move onto it from `db/strings`, now with types (`WRONGTYPE`), TTLs
> (`SET ... EX 10`) and `OBJECT ENCODING`. Every other data type, expiry,
> eviction, persistence and WATCH builds on this API. The surprising part: an
> expired key isn't deleted when it expires. It's deleted when someone next
> looks at it.
>
> **Before:** `ds/dict`, `db/strings`. **Next:** `db/expire`, `db/lists`,
> `db/hashes`, `db/sets`, `db/generic`.

## Values have types

```cpp
class Value { virtual Type type() const; virtual std::string_view encoding() const; ... };
class StringValue : Value { std::string s; bool raw; };     // here
class ListValue, HashValue, SetValue, ZSetValue             // later exercises
```

A key holds one value of one type. Commands check it with the given
`lookup<T>` helper. `GET` on a list replies
`WRONGTYPE Operation against a key holding the wrong kind of value`, while
`SET`, `DEL`, `EXISTS` and `TYPE` work on anything, and `MGET` quietly returns
nil for non-strings (all recorded from Redis).

`OBJECT ENCODING` shows how Redis stores a string:

| Value | Encoding |
|---|---|
| fits `parse_int64` (`"123"`, `"-5"`) | `int`: Redis stores the number itself, no string |
| up to 44 bytes | `embstr`: object header and bytes in one allocation |
| longer, or modified in place by APPEND/SETRANGE | `raw` |

You store every string as `std::string`; the encoding is only reported.

## Lazy expiry

`expires_` maps key to an absolute time in ms. Nothing happens when that time
passes. Instead, *every* access (`find`, `find_for_write`, `erase`) first
checks the key's TTL and deletes it if it's due, then carries on as if the
key were missing:

```
SET k v PX 100        expires_[k] = now + 100
(100 ms pass)         k is still in memory
GET k                 expired → delete k and its TTL → reply nil
```

That's free for keys nobody reads, but those would leak memory forever, so
`db/expire` adds an *active* cycle that samples TTLs in the background.

## Bookkeeping for later exercises

| Field | Who uses it |
|---|---|
| `Entry::lru`, set on every access | `db/eviction` (approximate LRU) |
| `dirty()`, incremented by writes | `persist/aof`, `repl/replica`: did this command write? |
| `set_on_modified(fn)` | `msg/transactions`: WATCH |
| `entries()`, `expires()` | `db/generic` (SCAN, RANDOMKEY), `db/expire`, `persist/rdb_writer` |

## SET's expiry options

`EX s`, `PX ms`, `EXAT unix-s`, `PXAT unix-ms`, `KEEPTTL`, at most one of them.
The number must be a positive integer, and seconds × 1000 must not overflow
(`SET k v EX 9223372036854775807` → `ERR invalid expire time in 'set'
command`). A time already in the past stores the key and expires it at once.
Without `KEEPTTL`, `SET` drops an existing TTL. In-place writes (`INCR`,
`APPEND`) keep it (`Commands.InPlaceWritesKeepTheTtl`).

## API

See `keyspace.h`. In short: `find`, `find_for_write`, `set(key, value,
keep_ttl)`, `erase`, `erase_if_empty`, `clear`, `size`, `is_expired`,
`set_expire`, `get_expire`, `persist`, `StringValue::encoding`, and
`register_keyspace_commands`: GET SET DEL EXISTS INCR DECR INCRBY DECRBY APPEND
STRLEN MGET MSET TYPE OBJECT.

Start the commands by copying your `db/strings` solution, then switch it to
`lookup<StringValue>` and `ks.set(...)`.

## Task

    bazel test //db/keyspace:keyspace_test
