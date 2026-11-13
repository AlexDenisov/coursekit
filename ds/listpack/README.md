# ds/listpack: Listpack

> **Summary.** One contiguous allocation holding a sequence of small strings
> and integers, each in as few bytes as possible, walkable in both directions.
> Redis stores every small hash, list node, set and sorted set this way, and
> RDB files embed the bytes unchanged. `db/encodings` uses yours. The
> surprising part: each entry ends with its own length written *backwards*,
> which is what lets you walk the list from the end.
>
> **Before:** nothing (`resp/parser`'s `parse_int64` is reused).
> **Next:** `db/encodings`.

## The layout

```
HSET k f v
0d 00 00 00 | 02 00 | 81 'f' 02 | 81 'v' 02 | ff
total = 13  | count | "f"  len  | "v"  len  | end
```

Each entry is `<encoding+data><backlen>`. The encoding's first byte says what
follows: a 7-bit integer in the byte itself, 13-bit or 16/24/32/64-bit
integers, or a string with a 6-bit, 12-bit or 32-bit length (the table is in
`listpack.h`). Strings that *look like* integers are stored as integers:
`"4096"` takes 3 bytes, not 5. "Look like" means exactly `parse_int64`'s
rules, so `"01"`, `"-0"` and `"+1"` stay strings (`Listpack.IntegerLikeStringsStayStrings`,
recorded from Redis).

## Walking backwards

The backlen stores the size of `<encoding+data>`, written so it can be read
right to left:

```
size 2     → 02              (≤ 127: one byte)
size 4097  → 20 81           (7 bits per byte; read from the end: 0x81 has the
                              high bit set, so "more to the left"; 0x20 ends it)
```

Start at the `ff` terminator, read the backlen just before it, and jump back
over that many bytes plus the backlen itself: that's the start of the last
entry. Repeat. That's how `LRANGE l -10 -1` or `RPOP` avoid walking from the
front. `to_vector_reversed` must use only this (`Listpack.WalksBackwards`).

## Why it replaced ziplist

Its predecessor stored each entry's *previous* entry's length at the front.
Growing one entry past 254 bytes could grow the next one's header, which could
grow the next, and so on: a "cascading update". Storing an entry's *own*
length after it makes every entry independent of its neighbours.

## API

| Function | Contract |
|---|---|
| `push_back`, `insert(i, v)`, `erase(i)` | rebuild the blob; update total and count |
| `get(i)`, `find(v)`, `size()` | integers come back as decimal strings |
| `to_vector()`, `to_vector_reversed()` | forwards; backwards via backlens only |
| `bytes()` / `from_bytes(b)` | Redis's exact layout; validating loader |

## Task

    bazel test //ds/listpack:listpack_test
