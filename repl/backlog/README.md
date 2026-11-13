# repl/backlog: Replication backlog

> **Summary.** A replica that loses its connection for two seconds shouldn't
> need the whole dataset again. The primary keeps the last N bytes of its
> replication stream in a ring buffer, the backlog. A reconnecting replica
> says "I have everything up to offset X", and if X + 1 is still in the
> backlog, the primary sends just the rest. `repl/replica` asks for exactly
> this with `PSYNC`. The surprising part: one number, the offset, is the whole
> protocol for "where were we?".
>
> **Before:** nothing. **Next:** `repl/replica`.

## Offsets

Every byte the primary ever sends to replicas has an offset; the first is 1.

```
stream:   a  b  c  d  e  f  g  h  i  j        capacity 8
offset:   1  2  3  4  5  6  7  8  9  10       offset() = 10
kept:           c  d  e  f  g  h  i  j        first_offset() = 3
```

A replica that processed bytes up to 8 sends `PSYNC <replid> 9`.
`since(9)` returns `"ij"`, a *partial* resync. A replica at 1 asks for 2,
which is gone: `since(2)` is `nullopt`, a *full* resync (a new RDB transfer).
A replica asking for 11 is up to date and gets nothing; asking for 12 claims
bytes that don't exist, so it gets a full resync too.

## The ring

A fixed array plus a head index. Appending writes at most two pieces: up to
the end of the array, then wrapped to the start. Reading the last n bytes
does the same backwards. The edge cases that bite:

- an append longer than the whole ring keeps only its tail
  (`Backlog.HugeAppendKeepsOnlyTheTail`);
- an empty backlog can't serve anyone, not even "nothing new"
  (`Backlog.Empty`).

`Backlog.MatchesTheWholeStream` compares random appends and requests against
the full stream as a plain string, for capacities from 1 to 1000.

## API

| Function | Contract |
|---|---|
| `Backlog(capacity)` | |
| `append(data)` | O(\|data\|) |
| `offset()`, `size()`, `first_offset()` | |
| `since(next)` | the bytes from `next` to `offset()`, or nullopt: full resync |

## Task

    bazel test //repl/backlog:backlog_test
