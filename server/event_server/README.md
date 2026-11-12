# server/event_server: ★ Event-driven server

> **Summary.** The second milestone: one thread, thousands of clients, the
> way Redis really works. Your event loop watches the listener and every
> client socket, and your `Client` objects do the reading, running and
> writing. `redis-benchmark -c 50 -P 16` runs clean against it.
> `server/mini_redis` adds the full keyspace on top. The surprising part: the
> hard part isn't concurrency, there is none. It's knowing when to stop
> watching for writability.
>
> **Before:** `net/event_loop`, `net/client`, `cmd/dispatch`, `db/strings`.
> **Next:** `server/mini_redis`.

## Wiring

```
listener readable  ->  accept every pending connection (until EAGAIN)
                       non-blocking, TCP_NODELAY, new Client, watch it for reading
client readable    ->  on_readable(); close if should_close(); else flush it
client writable    ->  on_writable(); nothing left? stop watching for writing
before sleep       ->  flush every client with pending replies
flush(client)      ->  write now; if the kernel didn't take everything,
                       watch for writing
```

## The pitfall: writable is almost always true

A socket is writable whenever its send buffer has room, which is nearly
always. If you watch every client for `kWritable` permanently, `kevent`
returns at once on every iteration and the server spins at 100% CPU doing
nothing. Watch for writing only while a client has replies the kernel didn't
take yet, and stop as soon as it's drained. Redis does the same, and it also
tries to write directly before sleeping, which usually succeeds and never
touches the event loop at all.

That before-sleep step also covers replies that appear for a client which
isn't the one running. Pub/sub delivers messages that way, with
`Session::push` (`EventServerTest.PushedRepliesAreFlushed`).

## Closing safely

Closing a client in the middle of its own handler is normal: EOF, a protocol
error, the query limit. Remove its events from the loop, *then* destroy the
`Client` (which closes the fd), and don't touch it afterwards. The loop's
stale-event check from `net/event_loop` covers the rest.

## Try it

```sh
bazel run -c opt //server/event_server -- 6380
redis-benchmark -p 6380 -c 50 -n 100000 -P 16 -t ping_mbulk,set,get -q
```

(`-t ping` would also run `PING_INLINE`, which needs the optional inline part of
`resp/parser`.) Open two `redis-cli -p 6380` at once: both work, unlike with
`server/hello_cli`.

## API

| Function | Contract |
|---|---|
| `EventServer(table, clock, limits)` | |
| `listen(host, port)` | non-blocking listener in the loop; returns the port |
| `run()`, `stop()` | the loop; `stop` is thread-safe |
| `client_count()` | connected clients |
| `set_new_client_hook(fn)` | called for every new `Client` (used by the Messaging track) |
| `main.cc` | given |

## Task

    bazel test //server/event_server:event_server_test
    bazel test -c opt //server/event_server:redis_benchmark_test --test_output=all
