# ds/intset: Intset

> **Summary.** A set of small integers as a hash table costs around 60 bytes
> per member; as a sorted array of 2-byte numbers, it costs 2. Redis stores
> every small set of integers this way, and the bytes go straight into RDB
> files. `db/encodings` uses yours for small integer sets, and
> `persist/rdb_reader` can load these blobs. The surprising part: one large
> number makes every element bigger.
>
> **Before:** nothing. **Next:** `db/encodings`.

## The layout (byte for byte as Redis)

```
SADD k 5 1 3 -2
02 00 00 00 | 04 00 00 00 | fe ff  01 00  03 00  05 00
encoding=2  | length=4    | -2     1      3      5        (int16, little-endian, sorted)
```

The tests compare your bytes with what real Redis embeds in `DUMP` payloads
(`tools/blob_reference.py`).

## Upgrades

All elements share one width. Add 70000 to a set of int16s and the whole
array is rewritten as int32s. Never the other way round: removing 70000
leaves the set at 4 bytes per element (`IntSet.RemoveNeverDowngrades`).

A neat consequence: a value that forces an upgrade is outside the old range,
so it's either smaller than everything (it goes first) or larger than
everything (it goes last). No search needed.

## The pitfall: mixed widths

- `contains(70000)` on an int16 set must say no *before* searching. Truncated
  to 16 bits, 70000 becomes 4464, which might be in the set
  (`IntSet.ContainsChecksTheRangeFirst`).
- Reading an element means sign-extending `encoding` bytes. `fe ff` is -2, not
  65534.

## API

| Function | Contract |
|---|---|
| `add(v)`, `remove(v)`, `contains(v)` | O(n), O(n), O(log n) |
| `size()`, `encoding()`, `at(i)` | |
| `bytes()` | Redis's exact layout |
| `from_bytes(b)` | validate and adopt a blob from an RDB file |

## Task

    bazel test //ds/intset:intset_test
