# repl/replica: Replica and PSYNC

> **Summary.** `REPLICAOF host port` turns your server into a live copy of
> another: it connects, receives a full snapshot, then applies every write the
> primary makes, forever. After a disconnect it asks for just what it missed.
> This is the replica side of Redis's protocol, and it works against a real
> `redis-server` primary (`real_primary_test`), which lists your server as a
> connected replica and tracks its offset. The surprising part: the
> replication stream is just RESP requests, so your parser and command table
> do all the applying.
>
> **Before:** `repl/backlog`, `persist/rdb_reader`, `server/event_server`
> (the non-blocking machinery). **Next:** `server/durable_redis`.

## The conversation (recorded from Redis 8.10)

```
replica → PING                              ← +PONG
replica → REPLCONF listening-port 6380      ← +OK
replica → REPLCONF capa psync2              ← +OK
replica → PSYNC ? -1                        ← +FULLRESYNC 5dc1e4b8...cfbe 0
                                            ← (maybe "\n" keepalives)
                                            ← $187\r\n REDIS0015... (no CRLF after the RDB)
                                            ← *2 SELECT 0, *3 SET after 2, ...   the live stream
replica → REPLCONF ACK <offset>             (every second)
```

After a disconnect it sends `PSYNC <replid> <offset+1>`. If the primary's
backlog (`repl/backlog`) still has those bytes, it answers `+CONTINUE` and
streams from there, with no RDB.

## Offsets

The offset counts *every byte of the stream* the replica has processed,
including `SELECT`, `PING`s and `REPLCONF GETACK`. It's not a count of
commands, and not of writes. If you skip counting something you didn't
"execute", your next PSYNC asks for the wrong byte, and the primary either
resends a command (applied twice: `INCR` corrupts) or skips one.
`ReplicaTest.FullSyncLoadsTheRdbThenAppliesTheStream` and
`ReplicaTest.AnswersGetack` pin the exact numbers.

## Everything is non-blocking

The replica lives inside your server's event loop: a connect that completes
on writability, a handshake that waits for one reply line at a time, an RDB
that arrives in pieces, a stream parsed as bytes come in, an ACK timer, and a
retry timer after any failure. One small state machine (`State`) tracks
where you are. A blocking read anywhere would freeze every client of the
replica.

## Not here

The *primary* side (serving PSYNC to replicas, filling a backlog from
propagated writes) is the mirror image and a good follow-up exercise. Real
Redis also does diskless sync (`capa eof`), which this replica doesn't
announce, so primaries fall back to sending a saved RDB.

## API

| Function | Contract |
|---|---|
| `Replica(ks, table, loop, listening_port)` | |
| `start(host, port)` / `stop()` | REPLICAOF host port / REPLICAOF NO ONE |
| `state()`, `replid()`, `offset()`, `full_syncs()`, `partial_syncs()` | |
| `register_replicaof_command(table, replica)` | REPLICAOF, SLAVEOF |

## Task

    bazel test //repl/replica:replica_test
    bazel test //repl/replica:real_primary_test --test_output=all   # follows a real redis-server
