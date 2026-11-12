# db/sets: Sets

> **Summary.** Unordered collections of unique strings, with set algebra on
> the server: `SINTER followers:ann followers:bob` without shipping either set
> to the client. A set is your dict with members as keys and nothing as
> values. `db/encodings` adds compact intsets and listpacks for small sets. The
> surprising part: intersection cost depends on which set you iterate.
>
> **Before:** `db/keyspace`, `ds/dict`. **Next:** `db/encodings`,
> `server/mini_redis`.

## The type

```cpp
class SetValue : public Value { Dict<bool> members; ... };   // the bool is unused
```

```
SADD s a b c        :3
SADD s a d          :1       (only d is new)
SISMEMBER s a       :1
SREM s a z          :1
SMEMBERS s          b c d    (in no particular order)
```

## Algebra

- `SINTER a b c`: members in all of them. A missing key is the empty set, so
  the result is empty.
- `SUNION a b`: members in any of them.
- `SDIFF a b c`: members of `a` in none of the others.
- `...STORE dst ...`: the same, stored at `dst` (replacing whatever was
  there, deleting it if the result is empty), replying the size.

A wrong-typed key anywhere in the list is `WRONGTYPE`, even if another key
would make the answer obvious. Check every key before computing.

## The pitfall: iterate the smallest set

`SINTER big small` with `|big| = 10^6` and `|small| = 1`. Walking `big` and
probing `small` costs a million lookups; walking `small` and probing `big`
costs one. Redis sorts the inputs by size first
(`Sets.IntersectionIteratesTheSmallestSet` runs 200 such intersections and
expects them to be instant).

## Optional: randomness

`SPOP` and `SRANDMEMBER` pick random members with `members.random_key(rng)`.
Their counts follow a subtle contract. `SRANDMEMBER s 3` gives up to 3
*distinct* members, while `SRANDMEMBER s -3` gives exactly 3 *with repeats*.
And `SPOP missing 2` is `*0`, while `LPOP missing 2` is a null array. Both
recorded from Redis.

## API

| Function | Contract |
|---|---|
| `SetValue::approx_bytes()` | rough size, for `db/eviction` |
| `register_set_commands(table, ks)` | SADD SREM SCARD SISMEMBER SMISMEMBER SMEMBERS SINTER SUNION SDIFF (+STORE) SMOVE |
| `register_set_optional_commands(table, ks, rng)` (optional) | SPOP, SRANDMEMBER |

## Task

    bazel test //db/sets:sets_test
    bazel test //db/sets:optional_test
