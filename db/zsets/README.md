# db/zsets: Sorted sets

> **Summary.** Leaderboards, priority queues, time-ordered indexes: a sorted
> set maps members to scores and keeps them in order. Redis stores two
> structures side by side, a skiplist for order and a dict for "what's the
> score of X?", and every write updates both. `server/lean_redis` builds a
> leaderboard on it. The surprising part: printing a double so it reads back
> exactly, and exactly as Redis prints it, is half the exercise.
>
> **Before:** `ds/skiplist`, `ds/dict`, `db/keyspace`. **Next:**
> `db/encodings`, `server/lean_redis`.

## Two structures, one set

```
ZADD lb 100 ann 250 bob 175 cy
scores (dict):    ann → 100, bob → 250, cy → 175        ZSCORE: O(1)
order (skiplist): (100,ann) → (175,cy) → (250,bob)      ZRANK, ZRANGE: O(log n)
```

Changing a score means looking up the old one in the dict, moving the node in
the skiplist, and updating the dict. `ZSetValue::set` and `remove` keep them in
step; everything else reads.

## Printing scores

Redis replies with scores as strings, so `format_score` is everywhere:

| Score | Redis prints | Rule |
|---|---|---|
| 100, 1e16 | `100`, `10000000000000000` | integral and ≤ 2^62: an integer |
| 1.5, 0.1, 1e-5 | `1.5`, `0.1`, `0.00001` | shortest round-trip digits, plain |
| 0.1 + 0.2 | `0.30000000000000004` | shortest digits that read back *exactly* |
| 1e20, 5e-324 | `1e+20`, `5e-324` | scientific beyond the plain range |
| 12345678901234567890 | `12345678901234567000` | above 2^62: the digits plus zeros |

"Shortest digits that round-trip" is a hard algorithm (Grisu, Ryu), but C++17's
`std::to_chars(double)` already produces them. Your job is the layout, a port
of the rules in `zsets.h`. `FormatScore.RoundTripsRandomDoubles` checks that
what you print reads back to the same double.

## ZADD's options

`NX` (only add), `XX` (only update), `GT`/`LT` (only move up/down; new members
are still added), `CH` (also count changed scores), `INCR` (behave like
ZINCRBY). Their incompatibilities each have their own error message, recorded
from Redis and listed in `zsets.h` in the order Redis checks them. Parse
*all* scores before changing anything: `ZADD z 1 a nan b` must leave `a` out.

## Ranges

`ZRANGE z 0 -1` uses list-style indexes. `ZRANGE z (1 +inf BYSCORE LIMIT 0 10`
uses score bounds (`(` is exclusive), starting from `first_from` and walking
`next()`. With `REV`, it starts from `last_until` and walks `backward`, and
the bounds swap places: `ZRANGE z +inf -inf BYSCORE REV`.

## API

| Function | Contract |
|---|---|
| `ZSetValue::set`, `remove` | keep skiplist and dict in step |
| `format_score(v)`, `parse_score(s, &v)` | Redis's formatting and parsing |
| `register_zset_commands(table, ks)` | ZADD ZINCRBY ZSCORE ZMSCORE ZCARD ZRANK ZREVRANK ZREM ZCOUNT ZRANGE ZRANGEBYSCORE |

## Task

    bazel test //db/zsets:zsets_test
