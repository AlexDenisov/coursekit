# db/encodings: Compact encodings

> **Summary.** Most hashes, sets and sorted sets in a real Redis are small: a
> user record with 6 fields, a set of 10 tags. Storing each one as a dict
> wastes most of its memory on pointers and bucket arrays. Redis stores small
> ones as an intset or a listpack and converts to a dict (or skiplist) only
> once they grow past configurable limits. This exercise builds those
> containers with Redis 8's exact conversion rules. The surprising part: an
> O(n) scan of 128 entries in one cache-friendly allocation beats an O(1)
> dict lookup in a pile of small allocations.
>
> **Before:** `ds/intset`, `ds/listpack`, `ds/dict`, `ds/skiplist`.
> **Next:** `server/lean_redis`.

## The thresholds (Redis 8 defaults)

| Type | Compact while... | Then |
|---|---|---|
| hash | ≤ 512 fields, every field and value ≤ 64 bytes | `hashtable` |
| set of integers | ≤ 512 members, all `parse_int64`-able | `intset` → `hashtable` (or `listpack`) |
| set | ≤ 128 members, each ≤ 64 bytes | `listpack` → `hashtable` |
| sorted set (optional) | ≤ 128 members, each ≤ 64 bytes | `listpack` → `skiplist` |

Conversions go one way: deleting back below a limit never converts back
(`CompactHash.ConvertsPastTheEntryLimit`). Every expected encoding in the tests
is what `OBJECT ENCODING` said in real Redis after the same operations
(`tools/cases/encodings.txt`).

## The set's three-way decision

An intset gets a member that isn't an integer. Does it become a listpack or a
hashtable? Redis converts to a listpack only if the result *fits* one: 127
integers + "x" gives a listpack (128 entries), while 128 integers + "x" goes
straight to hashtable (`CompactSet.IntsetToListpackOnlyIfItFits`). And "integer"
means `parse_int64`, so `"01"` takes a set out of intset mode.

## Order is visible

A listpack keeps insertion order, and real Redis's `HGETALL` and `SMEMBERS`
show it: `HSET h b 1 a 2 c 3` replies `b a c`, and an updated field keeps its
place. An intset is always sorted. Once converted, order is whatever the dict
gives. Clients shouldn't rely on any of it, but the tests check that you
reproduce it.

## Where this would plug in

These classes are standalone. To make your server's `OBJECT ENCODING` match
Redis for small values, you'd swap `HashValue::fields` and
`SetValue::members` for them. That's a good refactor after the milestone; the
course doesn't require it.

## API

| Class | Operations |
|---|---|
| `CompactHash` | `set`, `get`, `remove`, `size`, `encoding`, `items` |
| `CompactSet` | `add`, `remove`, `contains`, `size`, `encoding`, `members` |
| `CompactZSet` (optional) | `add`, `remove`, `score`, `size`, `encoding`, `items` |
| `EncodingLimits` | the thresholds, Redis 8's defaults |

## Task

    bazel test //db/encodings:encodings_test
    bazel test //db/encodings:optional_test
