# msg/transactions: MULTI/EXEC/WATCH

> **Summary.** On a single thread, every command is already atomic. A
> *transaction* makes several commands atomic together: queue them with
> `MULTI`, run them back to back with `EXEC`, with no other client in
> between. `WATCH` adds optimistic locking: "run this only if nobody touched
> these keys since I looked". `server/messaging_redis` builds a
> never-lose-an-increment counter on it. The surprising part: there's no
> rollback, and it doesn't need one.
>
> **Before:** `cmd/dispatch` (`check()`), `db/keyspace` (the on-modified
> hook). **Next:** `server/messaging_redis`.

## Queue, then run (recorded from Redis 8.10)

```
MULTI          +OK
SET k 1        +QUEUED
INCR k         +QUEUED
EXEC           *2  +OK  :2
```

Two kinds of errors behave differently:

| When | Example | Result |
|---|---|---|
| while queueing | unknown command, wrong arity (`table.check`) | error now; `EXEC` → `EXECABORT ...`, nothing runs |
| while running | `INCR` on a string | that command's reply is the error; the others run anyway |

No rollback, because a runtime error in Redis is a programming error (wrong
type), not a conflict. Undoing half a transaction would cost a lot to handle
a case that "shouldn't happen".

## WATCH: check-and-set

```
WATCH balance          remember: abort if balance changes
GET balance            → 100 (decide in the client: new = 100 - 30)
MULTI
SET balance 70
EXEC                   → *1 +OK, or *-1 (nil) if someone changed balance meanwhile: retry
```

`Keyspace` calls the on-modified hook on every write to a key. Keep a map
key → watching sessions, and mark them dirty. The pitfall is the word
"every": `DEL`, `RENAME`, `FLUSHALL` and writes by the watching client itself
all count (`Tx.EveryKindOfChangeCounts`). Your commands already go through
`set`, `erase` and `find_for_write`, which is why that hook was designed in
`db/keyspace`.

## API

| Function | Contract |
|---|---|
| `Transactions(ks)` | installs the keyspace hooks |
| `handle(table, s, args, reply)` | MULTI/EXEC/DISCARD/WATCH/UNWATCH and queueing; false = run normally |
| `in_multi(s)`, `executing()`, `forget(s)` | |

## Task

    bazel test //msg/transactions:transactions_test
