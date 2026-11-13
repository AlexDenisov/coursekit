# Build your own Redis

Learn how Redis works by building one in C++20, one tested exercise at a time.
You start with the bytes on the wire and finish with a single-threaded server
that `redis-cli` and `redis-benchmark` talk to unchanged. It has strings,
expiry, lists, hashes, sets and SCAN, and it passes a compatibility suite
recorded from real Redis. Advanced tracks (compact encodings, eviction, RDB/AOF,
replication, pub/sub, transactions) are planned beyond that.

Every expected reply in the tests was recorded from a real `redis-server`, so
when your tests pass, your server answers the way Redis does, down to the error
strings.

## The big picture

What happens between `redis-cli SET k v` and `+OK`:

1. **Bytes arrive** on a socket, possibly in pieces: `net/sockets`, then
   `net/nonblocking` and `net/buffers`.
2. **The event loop** decides whose bytes to read next, one thread for all
   clients: `net/timers`, `net/event_loop` (kqueue), `net/client`.
3. **The parser** turns bytes into `{"SET", "k", "v"}`: `resp/parser`.
4. **The command table** finds `set` and checks its arity: `cmd/dispatch`.
5. **The command** changes the keyspace, a hash table that rehashes a little
   at a time: `db/strings`, `ds/dict`, `db/keyspace`, `db/expire`, ...
6. **The reply** is encoded as RESP and written back when the socket is
   writable: `resp/encoder`, `net/client`.

The milestones assemble these into servers: ★ `server/hello_cli` (blocking,
one client), ★ `server/event_server` (many clients), ★ `server/mini_redis`
(the main goal). The narrative behind all of this is in
[docs/how_redis_works.md](docs/how_redis_works.md); read it now, and again
halfway through.

## The course

- **[docs/map.html](docs/map.html)**: open it in a browser. Click an exercise
  to see what it needs, in a workable order. Presets jump to each ★.
- **[docs/roadmap.md](docs/roadmap.md)**: every exercise by track and level.
- **[curriculum.json](curriculum.json)**: the source of both.

Six tracks, three levels. Basic gets you talking to `redis-cli`; intermediate
gets you a usable mini Redis; advanced is everything after.

| Track | ★ Milestone |
|---|---|
| Protocol | `server/hello_cli`: redis-cli connects; PING, ECHO, SET, GET, INCR work |
| Event loop | `server/event_server`: `redis-benchmark -c 50` runs clean |
| Keyspace | `server/mini_redis`: the compatibility suite passes |
| Internals | `server/lean_redis`: within 2x of real Redis |
| Durability | `server/durable_redis`: survives `kill -9`; real Redis loads your RDB |
| Messaging | `server/messaging_redis`: chat, job queue, WATCH-guarded counter |

**Start here:** [`resp/encoder`](resp/encoder/README.md), then
[`resp/parser`](resp/parser/README.md), [`cmd/dispatch`](cmd/dispatch/README.md),
[`db/strings`](db/strings/README.md).

## Setup (macOS)

```sh
brew install bazelisk redis     # Bazel (pinned by .bazelversion) and the real Redis
bazel test //common/...         # first run downloads a hermetic clang; then passes
bazel run :refresh_compile_commands   # compile_commands.json for clangd (VS Code, Zed, ...)
```

You never need the host compiler: Bazel fetches clang 23 and uses libc++ from
the macOS SDK (Xcode or the Command Line Tools must be installed).

## Workflow

Each exercise is a directory with a README (the idea and the task), a header
(the API and its contracts), a `.cc` with stubs marked `TODO(you)`, and tests
that fail until you're done. Write your code in the `.cc` file, on this branch,
and commit when you like.

```sh
bazel test //resp/parser/...                                   # one exercise
bazel test //resp/parser:parser_test                           # required part only
bazel test //resp/parser:optional_test                         # optional part
bazel test //resp/parser:parser_test --test_filter='Parser.Every*'   # some test cases
bazel test //resp/parser:parser_test --test_output=all         # see all output
bazel test --config=asan //resp/parser/...                     # AddressSanitizer + UBSan
python3 tools/progress.py                                      # where am I? (updates the map)
```

A broken contract (an `RD_CHECK`) aborts the whole test binary, so one bug can
show up as a wall of failures. Fix the first one and rerun.

**Stuck?** Ask the agent ("I'm stuck on resp/parser"). It gives the weakest
hint that unblocks you, and shows solution code only if you ask for it
explicitly.

## How the tests know what's right

Every expected value was recorded from Redis 8.10 by a script in `tools/`,
not computed by hand:

| Script | Records |
|---|---|
| `redis_reference.py tools/cases/<x>.txt` | replies, byte for byte (`--inc`: the milestones' compatibility tables) |
| `glob_reference.py` | which keys `KEYS <pattern>` matches |
| `crc64_reference.py`, `blob_reference.py` | CRC-64s, intset and listpack bytes, dug out of `DUMP` |
| `rdb_reference.py` | real `dump.rdb` files for the RDB reader |
| `aof_reference.py` | what Redis writes to its AOF |
| `messaging_reference.py` | pub/sub, MULTI/WATCH and BLPOP across several connections |

Try your own commands the same way: add a line to a case file and re-run, or
poke at a live server: `printf '*1\r\n$4\r\nPING\r\n' | nc localhost 6379`.

Some tests use the real thing and are tagged `manual` (they skip if Redis
isn't installed): `redis-cli` and `redis-benchmark` against your servers, your
throughput next to `redis-server`'s, real Redis loading your `dump.rdb`, and
your replica following a real primary.

```sh
bazel test //server/hello_cli:redis_cli_test //server/mini_redis:real_clients_test \
           //persist/rdb_writer:real_redis_loads_test //repl/replica:real_primary_test \
           //server/durable_redis:real_redis_test --test_output=all
bazel test -c opt //server/lean_redis:versus_redis_test --test_output=all
```

After the last milestone, `docs/future_topics.md` has ideas for where to go
next.

## Reference solutions

The orphan branch `redis-solutions`, checked out next to this repo as
`../coursekit-solutions`, holds a reference solution for every exercise
(spoilers!). Its `verify.sh` runs all the tests against them in a private copy,
which proves every exercise is passable. You don't need it to do the course.

## Glossary

| Term | Meaning |
|---|---|
| RESP | REdis Serialization Protocol: the wire format (`+OK\r\n`, `$5\r\nhello\r\n`, ...) |
| bulk string | a length-prefixed, binary-safe string in RESP |
| multibulk | an array of bulk strings: how clients send requests |
| inline command | a request typed as one plain-text line (`PING\r\n`) |
| arity | number of arguments a command takes, counting its name; negative = "at least" |
| pipelining | sending many requests without waiting for replies |
| keyspace | the top-level dictionary from key to value |
| rehashing | moving entries to a bigger (or smaller) hash table, incrementally in Redis |
| event loop | one thread waiting on many sockets (kqueue on macOS, epoll on Linux) |
| RDB / AOF | snapshot file / append-only command log: Redis's two persistence formats |

## License and origin

MIT ([LICENSE](LICENSE)), both for the template code and for the course. This
course was made from [coursekit](https://github.com/AlexDenisov/coursekit), a
template for learning by building; [PLAYBOOK.md](PLAYBOOK.md) and
[AGENTS.md](AGENTS.md) describe how it works.
