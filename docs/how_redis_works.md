# How Redis works

The story behind the exercises. Read it once at the start; most of it will
sound abstract. Read it again after `server/mini_redis`, when every paragraph
names something you've built.

## One thread, in memory

Redis is a dictionary from keys to values. It lives entirely in RAM and is
served by a single thread. Those two choices explain almost everything else.

**In memory** means no command waits for a disk. A GET is a hash lookup, a few
hundred nanoseconds. Even on a fast network, the time it takes the bytes to
arrive and leave dwarfs the work in between.

**Single-threaded** means no locks. Every command runs alone, start to finish,
so `INCR` is atomic for free, and `MULTI/EXEC` only has to queue commands, not
isolate them. The price is that one slow command blocks *everyone*. `KEYS *`
over ten million keys stalls every client for seconds. Much of Redis's design
exists to keep every step short: incremental rehashing, SCAN cursors, active
expiry that stops after a time budget, and lazy freeing of big values.

How does one thread serve 10,000 clients? It never waits on any one of them.
It asks the kernel which sockets are ready (`kqueue` on macOS, `epoll` on
Linux), reads what's there, runs every complete command, queues the replies,
and asks again. That's the **event loop** (`ae.c` in Redis, `net/event_loop`
here). A client that sends half a command just leaves its half in a buffer
until the rest arrives.

## RESP: a protocol you can read

Clients send arrays of bulk strings; servers reply with one of five types:

```
-> *3\r\n$3\r\nSET\r\n$1\r\nk\r\n$1\r\nv\r\n
<- +OK\r\n
```

It's text, so `nc` and `telnet` are debugging tools. It's length-prefixed where
it matters, so values are binary-safe. And it's cheap to parse: no escaping,
and you always know how many bytes to wait for. **Pipelining** comes free:
because each request gets exactly one reply, in order, a client can send 100
requests in one write and read 100 replies in one read. That's often a 10x
throughput win, and `redis-benchmark -P 16` measures it. (RESP3, from Redis 6,
adds maps, doubles and push messages; this course sticks to RESP2, which every
client still speaks.)

`resp/encoder`, `resp/parser`, `net/client`.

## The command table

Every command is a row: name, arity, function, and flags (write / read-only /
may block / ...). Dispatch is one hash lookup and one arity check. Everything
else (replication, AOF, ACLs, cluster routing, `COMMAND DOCS`) hangs off the
same table, which is why adding a command in Redis means adding a row, not
editing a switch.

`cmd/dispatch`.

## The keyspace: a dict that never stops the world

The keyspace is a hash table (`dict.c`) with chained buckets and power-of-two
sizes. When it fills up, it has to grow, and copying 10 million entries in one
go would freeze the server for a second. So Redis keeps **two tables** during
a resize and moves a few buckets on every operation. Lookups check both
tables, and inserts go to the new one. The resize finishes after enough
operations, and no single command ever pays for all of it.

Iterating while a table is resizing is harder. SCAN solves it with a cursor
that counts in **reverse binary**: it increments the high bits first. When the
table doubles, every bucket splits into two buckets that a reverse-binary
cursor visits consecutively. As a result, every key that exists for the whole
scan is returned at least once, even across resizes. It's one of the cleverest
small algorithms in Redis.

`ds/dict`, `ds/dict_scan`, `db/keyspace`, `db/generic`.

## Values have types, and types have encodings

A value is a string, list, hash, set, sorted set or stream. `TYPE` tells you
which; using the wrong command gives `WRONGTYPE`. Inside, each type has
several **encodings**, and that's where much of Redis's memory efficiency
comes from:

| Type | Small | Large |
|---|---|---|
| string | an integer in the pointer itself, or one short allocation | a plain buffer |
| list | listpack | quicklist (a linked list of listpacks) |
| hash | listpack | dict |
| set | intset, or listpack | dict |
| sorted set | listpack | skiplist + dict |

A **listpack** is one contiguous allocation holding many small entries: no
pointers, no per-entry allocation, cache-friendly. Lookups are O(n), which is
fine for 128 entries. Past a threshold (`hash-max-listpack-entries` and
friends), Redis converts the value to the "real" structure. `OBJECT ENCODING`
shows which one you got. Sorted sets pair a **skiplist** (ordered, O(log n)
rank via span counts) with a dict (O(1) member → score).

`db/strings` … `db/sets`, then the Internals track.

## Time: expiry

A key with a TTL is also stored in a second dict, `expires`, mapping key to
deadline. Two mechanisms remove it:

- **lazily**: every lookup first checks the deadline; an expired key is deleted
  and reported missing;
- **actively**: ten times a second, a time event samples 20 keys with a TTL,
  deletes the expired ones, and repeats while more than 25% of the sample was
  expired, within a CPU-time budget.

Lazy expiry alone would leak memory for keys nobody reads. Active expiry alone
would have to scan everything. Together they keep stale keys to roughly a
quarter of the volatile ones, with bounded work.

`db/expire`, `net/timers`.

## Memory: eviction

With `maxmemory` set, a write that would exceed it first evicts keys. "Least
recently used" would need a linked list touched on every read. Redis
approximates it instead: each object stores a 24-bit clock, and eviction
samples a few keys and evicts the oldest, keeping a small pool of good
candidates between rounds. With 10 samples it's statistically close to true
LRU, at no cost on reads. LFU uses the same 24 bits as a logarithmic counter.

`db/eviction`.

## Durability: fork and a log

**RDB** writes a compact snapshot of the whole dataset. To do it without
stopping, Redis calls `fork()`: the child sees a frozen copy of memory and
writes it out, while the parent keeps serving. The kernel's copy-on-write only
duplicates the pages the parent changes meanwhile. (That's why Redis avoids
rehashing during a save: moving entries would touch every page.)

**AOF** appends every write command, as RESP, to a log; on restart, Redis
replays it. `appendfsync everysec` loses at most a second of writes. The log
grows forever, so Redis periodically **rewrites** it from a snapshot of the
current data, again in a forked child.

**Replication** reuses both. A new replica gets an RDB (full sync), then the
live stream of write commands. If it disconnects briefly, it resumes from its
offset in the primary's **backlog**, a ring buffer of recent commands, with no
full resync.

The Durability track.

## Talking between clients

**Pub/Sub** sends a message to every subscriber of a channel, with no storage:
miss it and it's gone. **Blocking pops** (`BLPOP`) park a client until a list
has an element. The client waits in the event loop, not in a thread. **MULTI/
EXEC** queues commands and runs them back to back, which on one thread is
already atomic. **WATCH** adds optimistic locking: EXEC fails if a watched key
was touched since.

The Messaging track.

## Where to read the real thing

Redis's source is unusually readable. Once you've done an exercise, read the
matching file in [github.com/redis/redis/src](https://github.com/redis/redis/tree/unstable/src):
`networking.c` (parser, client buffers), `ae.c` + `ae_kqueue.c` (event loop),
`server.c` (command table, cron), `dict.c`, `t_string.c`, `expire.c`,
`evict.c`, `listpack.c`, `t_zset.c`, `rdb.c`, `aof.c`, `replication.c`.
