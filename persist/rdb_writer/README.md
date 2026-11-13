# persist/rdb_writer: RDB writer

> **Summary.** RDB is Redis's snapshot format: one compact binary file with
> every key, value and TTL, written by `SAVE`/`BGSAVE` and read at startup.
> You write the whole keyspace in it, and the test that matters is whether
> real Redis loads your file (`real_redis_loads_test`). `persist/bgsave` writes
> it from a forked child, `persist/rdb_reader` reads it back, and
> `repl/replica` ships it to replicas. The surprising part: a format designed
> in 2009 is still loadable by Redis 8, so you can write the old, simple
> encodings.
>
> **Before:** `persist/crc64`, `db/keyspace` and the value types.
> **Next:** `persist/rdb_reader`, `persist/bgsave`.

## The file

```
REDIS0011                          magic + version (4 ASCII digits)
FA "redis-ver" "7.0.0"             aux fields: metadata, ignored by loaders that don't know them
FE 00                              SELECTDB 0
FB 07 01                           RESIZEDB: 7 keys, 1 with a TTL (a hint for preallocation)
FC <8 bytes LE>                    the next key's expiry, unix ms
00 03 "ttl" 01 "v"                 type 0 (string), key, value
01 04 "list" 03 01"a" 01"b" 01"c"  type 1 (list): length, elements
...
FF <8 bytes LE>                    EOF, then CRC-64 of everything before
```

## Lengths and strings

Everything is built from two encodings. A *length* uses 1, 2, 5 or 9 bytes
depending on its size; the top two bits of the first byte say which. A
*string* is a length plus bytes, except when it's an integer that fits in 32
bits. Then it's `C0`/`C1`/`C2` plus 1/2/4 bytes, and `"12345"` costs 3 bytes
instead of 6. The tests check both against what real Redis's `DUMP` produces.

The pitfall: only strings that come back *identically* may be stored as
numbers. `"01"` would load as `"1"`, and `"2147483648"` doesn't fit 32 bits
(`WriteString.IntegerLookalikesStayStrings`).

## Legacy types

Redis 8 writes small values as listpacks and intsets (types 11–20). It still
*loads* the original types, which are much simpler to write: list = length +
elements, set = length + members, hash = length + field/value pairs, zset =
length + (member, 8-byte binary double). Write those.

## Atomic saves

Write to `dump.rdb.tmp`, `fsync` it, then `rename` it over `dump.rdb`.
`rename` is atomic, so a crash leaves either the old snapshot or the new one,
never half of one (`Snapshot.SaveIsAtomic`).

## API

| Function | Contract |
|---|---|
| `write_length(out, n)`, `write_string(out, s)` | the two building blocks, byte-exact with Redis |
| `write_snapshot(ks)` | the whole file in memory |
| `save_snapshot(ks, path)` | to disk, atomically |

## Task

    bazel test //persist/rdb_writer:rdb_writer_test
    bazel test //persist/rdb_writer:real_redis_loads_test --test_output=all   # needs redis-server
