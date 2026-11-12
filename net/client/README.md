# net/client: Client state machine

> **Summary.** One connection's state: a query buffer of bytes waiting to be
> parsed, a reply buffer of bytes waiting to be sent, and the rules for when
> to read, run, write and close. It's Redis's `client` struct and
> `networking.c`, stripped down. `server/event_server` owns one per
> connection, and the Messaging track uses its hooks (`push`, `block`,
> `set_runner`). The surprising part: reading less per turn makes the server
> fairer, and costs almost nothing.
>
> **Before:** `resp/parser`, `resp/encoder`, `net/buffers`.
> **Next:** `server/event_server`.

## The cycle

```
readable  ->  on_readable: read <= 16 KB into query_  ->  process_input:
                 parse, run, append replies until incomplete
writable  ->  on_writable: write as much of replies_ as the kernel takes
```

The server calls these from event-loop handlers. Because kqueue is
level-triggered, a client with more input waiting is simply readable again on
the next iteration. Reading at most one chunk per event means a client
blasting 100 MB can't monopolise the thread: everyone gets a turn every
iteration (`ClientTest.ReadsAtMostOneChunkPerEvent`).

## Closing rules

| Situation | Behaviour (as Redis) |
|---|---|
| EOF or read/write error | close now; pending replies are dropped |
| protocol error | the replies so far, then the error, then close once sent; nothing after the error runs |
| more than `max_query_buffer` unparsed bytes | close now, no reply |
| `close_after_reply()` | close once the reply buffer is empty |

The query limit is checked on *unparsed* bytes, after processing, so a large
batch of complete requests is fine. A single endless request is not
(`ClientTest.QueryBufferLimitClosesWithoutReply`).

## Hooks for later

- `push(bytes)`: `Client` is a `Session`. Pub/sub delivers messages by
  appending to the reply buffer of *another* client.
- `block()` / `unblock()`: a client waiting in `BLPOP` must not run its next
  request until BLPOP has its answer. Its input just waits in `query_`.
- `set_runner(...)`: replaces "dispatch this request". `MULTI` queues requests
  instead of running them.

## API

| Function | Contract |
|---|---|
| `Client(fd, table, limits)` | owns the non-blocking socket |
| `on_readable()` | one read, then `process_input()`; returns bytes read |
| `process_input()` | run complete requests (unless blocked or after a protocol error) |
| `on_writable()` | write pending replies |
| `wants_write()`, `reply_bytes()` | pending output |
| `should_close()` | see the table |
| `push`, `block`, `unblock`, `set_runner`, `close_after_reply` | hooks |

## Task

    bazel test //net/client:client_test
