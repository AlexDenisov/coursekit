# ds/dict_scan: SCAN cursors

> **Summary.** `KEYS *` walks the whole keyspace in one command and blocks
> the server. `SCAN` walks it a few buckets per call, handing the client a
> cursor to continue from, while other clients keep inserting and deleting
> and the table keeps resizing. `db/generic` builds SCAN on this. The
> surprising part: the cursor counts with its bits reversed, and that one
> trick makes resizing harmless.
>
> **Before:** `ds/dict`. **Next:** `db/generic`.

## The guarantee

From cursor 0 back to cursor 0, every key that existed for the *whole* scan is
returned at least once. Keys added or deleted during the scan may or may not
appear, and some keys may come back twice. No state is kept on the server:
the cursor is the only state.

## Why counting normally fails

Say the table has 8 buckets and you've scanned buckets 0–3 (cursor = 4). Now it
shrinks to 4 buckets. Bucket `i` of the new table holds what was in old buckets
`i` and `i + 4`. Continuing at 4 & 3 = 0 rescans everything. Growing is worse:
old bucket 1 splits into new buckets 1 and 9, and a plain counter that has
passed 1 but not reached 9 sees part of it twice, and with deletes can skip
keys (`DictScan.SurvivesShrinkingBetweenCalls` fails for a plain counter).

## Reverse binary

Increment the cursor's bits *from the top*:

```
mask 7 (8 buckets):  000 → 100 → 010 → 110 → 001 → 101 → 011 → 111 → 000
                       0     4     2     6     1     5     3     7
```

When a table doubles, bucket `b` splits into `b` and `b + size`, which differ
only in the new *top* bit. In reverse-binary order those two are visited
consecutively. Whatever has been visited before the resize corresponds exactly
to a set of whole buckets after it, in both directions. Shrinking merges
buckets that differ in the top bit, which reverse-binary order also visits
together.

The step function: set every bit above the mask (so the carry runs off the
end), reverse all 64 bits, add one, reverse back.

## During a rehash

Both tables exist. Visit the *smaller* table's bucket `cursor & m_small`,
then every bucket of the larger table that it expands into: those are the
indices that agree with the cursor on the low bits. Keep advancing the cursor
with the large mask while it has bits set in `m_small ^ m_large`.

## API

| Function | Contract |
|---|---|
| `scan_next_cursor(cursor, mask)` | the reverse-binary increment |
| `dict_scan(d, cursor, fn)` | `fn(key, value)` for the cursor's bucket(s); the next cursor, 0 when done |

`dict_scan` is a template: write it in `dict_scan_impl.h`.

## Task

    bazel test //ds/dict_scan:dict_scan_test
