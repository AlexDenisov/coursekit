# net/sockets: Blocking sockets

> **Summary.** Before any event loop, plain blocking TCP: listen, accept, read,
> write. `server/hello_cli` is built on exactly these calls, and
> `net/nonblocking` changes one flag and then deals with the consequences. The
> surprising part: `read` and `write` move *some* bytes, not the bytes you
> asked for, and writing to a hung-up client kills your process by default.
>
> **Before:** nothing. **Next:** `server/hello_cli`, `net/nonblocking`.

## The server side, in five calls

```
socket(AF_INET, SOCK_STREAM, 0)       a TCP endpoint
setsockopt(SO_REUSEADDR)              see the pitfalls
bind(127.0.0.1:6379)                  claim an address
listen(backlog)                       start queueing incoming connections
accept()  ->  a new fd per client     blocks until someone connects
```

Port 0 means "any free port". The tests use it so they never collide, and
`local_port` (via `getsockname`) tells you which port you got. The client side
is `socket` + `connect`.

## The pitfalls

1. **Short reads are normal.** TCP is a byte stream, not a message stream. A
   client's `write("*1\r\n$4\r\nPING\r\n")` can arrive as three reads, or merge
   with the next request into one. `read_some` returns whatever is there.
   Finding request boundaries is `resp/parser`'s job.
2. **Short writes too.** `write(fd, 8 MB)` may return 147456: the kernel took
   what fit in its buffer. `write_all` loops until everything is out
   (`Sockets.WriteAllHandlesShortWrites`). The same applies to `EINTR`: a
   signal interrupted the call, so try again.
3. **SIGPIPE.** Write to a connection whose peer has closed it, and the kernel
   sends you SIGPIPE, whose default action is to terminate the process. One
   client hanging up mid-reply would kill your whole server. Ignore the signal
   (Redis does: `signal(SIGPIPE, SIG_IGN)`) or, on macOS, set `SO_NOSIGPIPE`
   per socket. Then `write` simply fails with `EPIPE`
   (`Sockets.WriteToClosedPeerFailsWithoutKillingTheProcess`).
4. **SO_REUSEADDR.** After a server closes a connection, the port lingers in
   `TIME_WAIT` for a minute or two, and a restarted server's `bind` fails with
   "address already in use". Set `SO_REUSEADDR` before `bind`
   (`Sockets.PortIsReusableRightAfterClose`).

## The echo server

`echo_one_client` puts it together: accept one client, then `read_some` and
`write_all` until `read_some` returns 0. Zero means the client called
`shutdown` or `close`: end of stream.

## API

| Function | Contract |
|---|---|
| `Fd` | given: owns an fd, closes it on destruction, move-only |
| `listen_tcp(host, port, backlog)` | IPv4, `SO_REUSEADDR`, port 0 = any |
| `local_port(fd)` | the bound port |
| `accept_connection(listen_fd)` | one client; retries `EINTR` |
| `connect_tcp(host, port)` | a connected socket |
| `read_some(fd, buf, n)` | > 0 bytes, 0 = EOF, -1 = error; retries `EINTR` |
| `write_all(fd, data)` | everything or false; never SIGPIPE |
| `echo_one_client(listen_fd)` | echo until EOF; bytes echoed |

## Task

    bazel test //net/sockets:sockets_test

Try it by hand with a tiny `main` (or later `server/hello_cli`) and
`nc localhost <port>`.
