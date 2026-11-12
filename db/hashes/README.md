# db/hashes: Hashes

> **Summary.** A hash is a small map stored at one key: `user:1 → {name: ann,
> visits: 3}`. In Redis it's literally a dict inside the keyspace dict, and here
> it's your `ds/dict` inside your `ds/dict`. `persist/rdb_writer` saves them, and
> `db/encodings` gives small ones a compact representation. The surprising part:
> `HGETALL` has no defined order, and real Redis's order changes when a hash
> grows past 128 fields.
>
> **Before:** `db/keyspace`, `ds/dict`. **Next:** `db/encodings`,
> `server/mini_redis`.

## The type

```cpp
class HashValue : public Value { Dict<std::string> fields; ... };
```

Every command is a lookup of the key (`lookup<HashValue>`), then dict
operations on the fields:

```
HSET user:1 name ann visits 3     :2      (two new fields)
HINCRBY user:1 visits 1           :4
HGET user:1 name                  $3 ann
HDEL user:1 name visits           :2      and user:1 is gone: no fields left
```

## Order

Small hashes in real Redis are listpacks and reply in insertion order. Large
ones are dicts and reply in bucket order. Yours reply in your dict's order. The
tests compare `HGETALL`, `HKEYS` and `HVALS` as multisets: `HGETALL` as a set
of field/value *pairs*, so a reply that mixes up which value belongs to which
field still fails. Clients that need an order must sort.

## Details that are easy to miss

- `HSET` takes pairs: `HSET h f` and `HSET h a b c` are arity errors, although
  the minimum arity (-4) lets the second through. Check the count is even.
- `HSET` replies how many fields were *new*, not how many were written.
- `HINCRBY` has two different integer errors: a bad increment (`ERR value is
  not an integer or out of range`) and a field that doesn't hold an integer
  (`ERR hash value is not an integer`).
- Deleting the last field deletes the key (`ks.erase_if_empty`).

## API

| Function | Contract |
|---|---|
| `HashValue::approx_bytes()` | a rough size, for `db/eviction` |
| `register_hash_commands(table, ks)` | HSET HSETNX HGET HMGET HDEL HLEN HEXISTS HSTRLEN HGETALL HKEYS HVALS HINCRBY; see `hashes.h` |

## Task

    bazel test //db/hashes:hashes_test
