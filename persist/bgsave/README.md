# persist/bgsave: BGSAVE with fork

> **Summary.** Writing a 10 GB snapshot takes a while, and a single-threaded
> server can't stop for that. Redis calls `fork()`. The child gets a frozen,
> private view of memory and writes it out at leisure, while the parent keeps
> serving. The kernel shares the memory between them and copies a page only
> when the parent writes to it. `server/durable_redis` runs BGSAVE from a
> command and on a schedule, and `persist/aof_rewrite` uses the same trick.
> The surprising part: a whole-database consistent snapshot costs one system
> call.
>
> **Before:** `persist/rdb_writer`, `net/event_loop`. **Next:**
> `persist/aof_rewrite`, `server/durable_redis`.

## The dance

```
parent: pid = fork()
child:  save_snapshot(ks, "dump.rdb")   ← sees memory as of the fork
        _exit(ok ? 0 : 1)               ← _exit: no destructors, no flushing parent buffers
parent: returns at once; a timer every 10 ms calls waitpid(pid, WNOHANG)
parent: child exited 0 → last save = now
```

`BgSave.TheSnapshotIsThePointInTimeOfTheFork` changes 20 000 values right
after the fork and checks that the file still has the old ones. That's
copy-on-write doing the work.

## The pitfalls

- **`exit` vs `_exit`.** The child is a copy of the whole server: open
  sockets, buffered stdio, objects with destructors. `exit()` runs atexit
  handlers and flushes stdio buffers that the parent will flush too.
  `_exit()` leaves immediately.
- **Zombies.** A child that exited stays a zombie until the parent
  `waitpid`s it. Poll from a timer, with `WNOHANG` so the loop never blocks.
- **Copy-on-write costs memory.** Every page the parent writes during the
  save gets duplicated. A parent that rehashes a big dict during a save
  touches *every* page, doubling memory for nothing. That's why Redis pauses
  dict resizing while a child exists.
- **One at a time.** A second BGSAVE while one runs is refused, and so is
  SAVE: two writers on the same `dump.rdb.tmp` would interleave.

## API

| Function | Contract |
|---|---|
| `BackgroundSaver::start()` | fork; the child saves and `_exit`s; the parent polls from a timer |
| `poll()`, `in_progress()`, `last_status()`, `last_save_ms()` | |
| `save_now()` | SAVE, in-process |
| `register_save_commands(table, saver)` | SAVE, BGSAVE, LASTSAVE (replies recorded from Redis) |

## Task

    bazel test //persist/bgsave:bgsave_test
