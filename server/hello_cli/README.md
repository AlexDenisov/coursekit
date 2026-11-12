# server/hello_cli: ★ Hello, redis-cli

> **Summary.** The first milestone: your parser, command table, string commands
> and sockets become a server that the real `redis-cli` talks to. It serves one
> client at a time, with blocking calls, and that limitation is exactly what
> the Event loop track fixes next. The surprising part: once the pieces are
> right, the server loop is about 25 lines.
>
> **Before:** `resp/parser`, `resp/encoder`, `cmd/dispatch`, `db/strings`,
> `net/sockets`. **Next:** `server/event_server` (many clients at once).

## The loop

```
accept a client
loop:
    n = read_some(...)                 0 or error: the client is gone; return
    input += the n bytes
    while parse_request(input) is kOk:
        dispatch(args) -> replies      (skip empty requests)
        drop the consumed bytes
    kError: replies += the error; send; close
    write_all(replies)                 one write for the whole batch
```

Collecting all replies for one read and writing them once is what makes
pipelining fast. 200 `INCR`s in one packet get 200 replies in one packet, not
200 system calls (`HelloCli.PipelinedRequests`).

The input buffer outlives each read. A request that arrives in five pieces sits
there, `kIncomplete`, until the fifth piece completes it
(`HelloCli.RequestArrivingByteByByte`).

## The pitfall: errors mid-batch

If a client sends `PING`, `ECHO hi`, garbage, `PING` in one packet, Redis
replies `+PONG`, `$2 hi`, then the protocol error, and closes. The last `PING`
is never run (`HelloCli.ProtocolErrorRepliesThenCloses`, recorded from Redis).
Don't throw away the replies you already collected when you hit the error.

## Try it

```sh
bazel run //server/hello_cli -- 6380
redis-cli -p 6380                       # in another terminal
127.0.0.1:6380> SET greeting "hello world"
OK
127.0.0.1:6380> INCRBY n 41
(integer) 41
```

While `redis-cli` is connected, a second `redis-cli` hangs: the server is
blocked in `read` on the first one. That's the motivation for the next track.
(`redis-cli` asks `COMMAND DOCS` when it starts; it gets "unknown command" back
and carries on without command hints.)

## API

| Function | Contract |
|---|---|
| `BlockingServer::listen(host, port)` | given |
| `BlockingServer::serve_one()` | one client, start to finish |
| `BlockingServer::run()` | given: `serve_one` forever |
| `main.cc` | given: wires your commands into the server |

## Task

    bazel test //server/hello_cli:server_test
    bazel test //server/hello_cli:redis_cli_test     # with the real redis-cli
