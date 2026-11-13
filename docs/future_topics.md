# Future topics

Ideas the course doesn't cover yet, roughly in order of how naturally they
follow from what's there. Ask the agent to turn any of them into exercises
(they'd go into `curriculum.json` like the rest).

## Finishing what's started

- **Wire the compact encodings in.** Make `HashValue` and `SetValue` use
  `CompactHash`/`CompactSet` (`db/encodings`), so `OBJECT ENCODING` matches
  Redis for small values and memory drops. Then measure it with
  `server/lean_redis`.
- **The primary side of replication.** Serve `PSYNC`: send an RDB for a full
  sync (BGSAVE to a socket), fill a `repl/backlog` from propagated writes,
  answer `+CONTINUE` from it. Then your server can replicate to your server.
- **Output buffer limits.** Disconnect pub/sub clients whose reply buffer
  passes a limit (`client-output-buffer-limit pubsub 32mb 8mb 60`).
- **Replicas that log.** Writes applied from the replication stream should go
  to the replica's own AOF too.
- **Lazy freeing.** Deleting a 10-million-element set blocks the thread.
  Redis frees big values on a background thread (`UNLINK`, `lazyfree-*`).

## New ground

- **RESP3** (`HELLO 3`): maps, sets, doubles, push messages for pub/sub.
- **Streams** (`XADD`, `XRANGE`, consumer groups): a radix tree of listpacks.
- **Keyspace notifications**: pub/sub messages on every write.
- **Lua scripting** (`EVAL`) or Redis Functions: atomic server-side logic.
- **Cluster**: hash slots (`CRC16(key) % 16384`), `MOVED`/`ASK` redirects,
  gossip.
- **epoll**: port `net/event_loop` to Linux, and compare with kqueue.
- **I/O threads**: Redis 6+ reads and writes sockets on helper threads while
  commands stay single-threaded. Measure it with `redis-benchmark`.
- **ACLs and AUTH**: users, permissions per command and key pattern.
- **HyperLogLog** (`PFADD`/`PFCOUNT`): cardinality estimates in 12 KB.
- **Geo commands**: geohashes on top of sorted sets.
