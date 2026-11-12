# Project specifics

This course's answers to the "Project specifics" questions at the end of
[PLAYBOOK.md](../PLAYBOOK.md). The playbook stays generic; everything
particular to this course goes here. Filled in by the agent during
bootstrap (see [AGENTS.md](../AGENTS.md)) and kept up to date.

- **Topic / end goal:** how Redis works, learned by building it. Main goal:
  `server/mini_redis`, a single-threaded event-loop server that `redis-cli`
  and `redis-benchmark` use unchanged, with strings, expiry, lists, hashes,
  sets, KEYS/SCAN, and a compatibility suite recorded from real Redis.
  Advanced tracks (internals, durability, messaging) are planned beyond that.
- **Learner:** uses Redis from the outside; new to its internals, to
  systems programming (sockets, event loops, fork) and to the underlying data
  structures, so start from first principles. 5–10 h/week. macOS on Apple
  silicon (kqueue, not epoll).
- **Tracks (goals) and milestones (★):**
  - Protocol → ★ `server/hello_cli` (basic)
  - Event loop → ★ `server/event_server` (intermediate)
  - Keyspace → ★ `server/mini_redis` (intermediate, the main goal)
  - Internals → ★ `server/lean_redis` (advanced)
  - Durability → ★ `server/durable_redis` (advanced)
  - Messaging → ★ `server/messaging_redis` (advanced)
- **Levels:** basic / intermediate / advanced (the template's default).
  Basic = talk to redis-cli; intermediate = usable mini Redis; advanced =
  everything after.
- **Language, toolchain, and deviations from PLAYBOOK §11–12:** C++20, Bazel
  + hermetic clang, GoogleTest (the `starters/cpp-bazel` stack). Event loop
  uses kqueue only (macOS); an epoll backend is a possible side track. RESP2
  only (RESP3 is a future topic).
- **Namespace / check macro prefix:** `rd` / `RD_` (Bazel module `rd`).
- **Oracle(s):**
  - Real `redis-server` (`brew install redis`):
    `tools/redis_reference.py` sends each test's command sequence to a
    throwaway real Redis on a random port and records the exact reply bytes.
    Used for RESP, command semantics, error strings, score formatting,
    OBJECT ENCODING, and the milestone compatibility suites.
  - Real Redis as a file oracle: it must load our `dump.rdb`; its own
    `dump.rdb` files are fixtures for `persist/rdb_reader`.
  - Brute-force models: `std::unordered_map` / `std::map` / `std::set` for
    `ds/dict`, `ds/skiplist`, `ds/intset`, `ds/listpack` randomised tests; a
    naive recursive matcher for `db/glob` (small inputs only).
  - Published check values (CRC-64 Jones of `"123456789"` =
    `0xe9c6d914c4b8d9ca`).
- **Real data (download scripts, env vars):** none needed. Milestones use
  `redis-cli` / `redis-benchmark` from Homebrew; tests that need them are
  tagged `manual` and skip when the binaries aren't on `PATH`.
- **Domains (directories):** `resp/`, `net/`, `cmd/`, `ds/`, `db/`,
  `persist/`, `repl/`, `msg/`, `server/`.
- **Given vs built by the learner:** everything in the curriculum is built by
  the learner. Given infrastructure lives in `common/` (checks, RNG, a tiny
  independent RESP test client and server-process helpers for tests, a fake
  clock).
- **Branch:** the course's main branch is `redis` (treated as `main`
  everywhere in AGENTS.md). Solutions on the orphan branch `solutions`.
- **License:** MIT for both the template and the learner's work (the existing
  `LICENSE` covers everything).
