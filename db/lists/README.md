# db/lists: Lists

> **Summary.** Redis lists are double-ended queues: push and pop at both ends
> in O(1), index by position (negative counts from the tail). They're the
> basis of job queues (`LPUSH` + `BRPOP`, which `msg/blocking` adds) and
> capped logs (`LPUSH` + `LTRIM`). `persist/rdb_writer` saves them. The
> surprising part: the index rules have more cases than the data structure.
>
> **Before:** `db/keyspace`. **Next:** `msg/blocking`, `server/mini_redis`.

## The type

```cpp
class ListValue : public Value { std::deque<std::string> items; ... };
```

`std::deque` gives O(1) at both ends, which is what Redis needs. Real Redis
uses a *quicklist*: a linked list of listpacks, compact arrays of small
entries. `ds/listpack` builds one of those; here a deque is enough.

## Indexes

```
items:      c   b   a   x   y
index:      0   1   2   3   4
negative:  -5  -4  -3  -2  -1
```

`LRANGE l start stop` is inclusive at both ends, and forgiving:

| Request | Result | Why |
|---|---|---|
| `LRANGE l -100 100` | everything | start clamps to 0, stop to the last index |
| `LRANGE l 3 1` | `*0` | start > stop |
| `LRANGE l 5 10` | `*0` | start past the end |
| `LRANGE l -1 -5` | `*0` | after adding the length: 4 > 0 |

Write the normalisation once. `LTRIM` uses the same rules to decide what to
*keep*.

## Empty lists don't exist

When `LPOP`, `RPOP`, `LTRIM` or `LREM` remove the last element, the key must
disappear: `EXISTS` 0, `TYPE` none (`Lists.PoppingTheLastElementDeletesTheKey`).
Call `ks.erase_if_empty(key)` after every removal. The reverse also holds:
`LPUSHX`/`RPUSHX` won't create a list, and a missing key behaves like an empty
list for reads (`LLEN` 0, `LRANGE` `*0`).

## LPOP with a count

`LPOP l` replies a bulk string or null. `LPOP l 2` replies an *array*: up to 2
elements, `*0` for a count of 0, and a *null array* (`*-1`) for a missing key.
A bad count gives `ERR value is out of range, must be positive`, also for
`abc` (Redis's message, recorded).

## API

| Function | Contract |
|---|---|
| `register_list_commands(table, ks)` | LPUSH RPUSH LPUSHX RPUSHX LPOP RPOP LLEN LRANGE LINDEX LSET LTRIM; see `lists.h` |
| `register_list_optional_commands` (optional) | LREM, LINSERT |

## Task

    bazel test //db/lists:lists_test
    bazel test //db/lists:optional_test
