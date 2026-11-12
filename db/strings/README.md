# db/strings: String commands

> **Summary.** These are your first real commands: GET, SET, DEL, EXISTS, the
> INCR family, APPEND, STRLEN, MGET, MSET. `server/hello_cli` serves them to
> `redis-cli`, and `server/event_server` serves them to `redis-benchmark`.
> `db/keyspace` later moves them onto your own hash table, with type checks.
> The surprising part: Redis has no integer type. `INCR` parses text, adds, and
> stores text again, and every step has an edge case.
>
> **Before:** `cmd/dispatch`, `resp/parser` (only `parse_int64`).
> **Next:** `server/hello_cli`, `db/keyspace`.

## The database is a map

For now the whole database is `StringDb::map`, a
`std::unordered_map<std::string, std::string>`. Each command is a lambda that
captures it:

```cpp
table.add({"strlen", 2, [&db](CommandContext& c) {
  auto it = db.map.find(c.args[1]);
  resp::append_integer(c.reply, it == db.map.end() ? 0 : it->second.size());
}});
```

`dispatch` has already checked the arity. Each command appends one reply.
`strings.h` lists every command with its arity and exact behaviour, and the
tests are transcripts recorded from Redis, replayed step by step.

## Small surprises

```
SET k v XX        on a missing key      ->  $-1        (null: not set)
SET k v NX GET    on an existing key    ->  $2 v3      (not set, old value returned)
EXISTS a a nokey                         ->  :2         (each argument counts)
DEL a a                                  ->  :1         (each key is removed once)
SET k ""; EXISTS k                       ->  :1         (empty is not missing)
```

## Integers are text

`INCR k` reads the value with `resp::parse_int64`, the same strict rules as
the parser. `"01"`, `" 1"`, `"+1"`, `"-0"` and `"1.5"` all fail with
`ERR value is not an integer or out of range`. A missing key counts as 0. The
result is stored as decimal text, so `APPEND` and `INCR` can be mixed:

```
SET num 10; APPEND num 5; INCR num    ->  :106
```

## The pitfall: overflow, twice

1. `old + by` can overflow, and signed overflow is undefined behaviour in C++.
   Check before adding: if `by > 0` and `old > INT64_MAX - by`, it overflows
   (and similarly below zero). Redis replies `ERR increment or decrement would
   overflow` and leaves the value as it was (`Strings.IncrOverflowIsAnError`).
2. `DECRBY k n` is `INCRBY k -n`, except when `n` is INT64_MIN, whose negation
   doesn't exist. Redis has a separate error for that, `ERR decrement would
   overflow` (`Strings.DecrbyInt64MinIsItsOwnError`). This is the same trap as
   in `resp/encoder`'s `format_int64`.

Write the arithmetic once, in a helper that takes the increment, and call it
from all four commands.

## Optional: GETRANGE and SETRANGE

`GETRANGE` has four clamping rules, and their order matters (`strings.h`
spells it out). `SETRANGE` pads with zero bytes. The pitfall:
`SETRANGE k 9223372036854775807 x` must be rejected by the 512 MB check
*before* you resize anything, and `offset + length` itself can overflow
(`Ranges.SetrangeBeyond512MbIsRejectedBeforeAllocating`).

## API

| Function | Contract |
|---|---|
| `register_string_commands(table, db)` | GET SET DEL EXISTS INCR DECR INCRBY DECRBY APPEND STRLEN MGET MSET (see `strings.h`) |
| `register_string_range_commands(table, db)` (optional) | GETRANGE SETRANGE |

## Task

    bazel test //db/strings:strings_test
    bazel test //db/strings:optional_test

Try any command against real Redis: add it to `tools/cases/strings.txt` and run
`python3 tools/redis_reference.py tools/cases/strings.txt`.
