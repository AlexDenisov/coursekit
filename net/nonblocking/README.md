# net/nonblocking: Non-blocking I/O

> **Summary.** One thread serving many clients must never wait for any one of
> them. With `O_NONBLOCK`, `read` and `write` return at once, and "nothing to
> read" or "no room to write" become an error code, `EAGAIN`, instead of a
> wait. `net/event_loop` tells you *when* to call these functions, and
> `net/client` calls them. The surprising part: the error you'll see most often
> means "everything is fine, come back later".
>
> **Before:** `net/sockets`, `net/buffers`. **Next:** `net/event_loop`,
> `net/client`.

## Four outcomes of a non-blocking read

| `read()` returns | Meaning | `IoResult` |
|---|---|---|
| n > 0 | n bytes | `bytes += n`, keep reading |
| 0 | the peer closed its side | `eof` |
| -1, `EAGAIN` / `EWOULDBLOCK` | nothing more right now | stop; not an error |
| -1, `EINTR` | interrupted by a signal | try again |
| -1, anything else | a real error | `error`, `err = errno` |

`read_available` loops until one of the stopping outcomes, appending straight
into the buffer with `prepare`/`commit`. The `max` argument lets a caller cap
one call. `net/client` uses it to read at most 16 KB per turn, so one fast
client can't starve the others.

Writes mirror reads. `write_available` writes from the front of the buffer
until it's empty or the kernel says `EAGAIN`, then consumes what was written.

## The pitfall: partial writes

A blocking `write` of 2 MB waits until all of it is gone. A non-blocking one
returns after the kernel buffer is full, perhaps 200 KB later, and the other
1.8 MB is *your* problem. Keep it, in order, and send it when the socket is
writable again (`Nonblocking.PartialWritesKeepTheRest`). In a server, "writable
again" is an event from the event loop. Forgetting the remainder corrupts the
stream for that client silently.

## Non-blocking connect

`connect` on a non-blocking socket returns `EINPROGRESS` at once. When the
socket becomes writable, the attempt has finished, successfully or not.
`getsockopt(SO_ERROR)` tells you which. You'll need this in `repl/replica`,
which connects to a primary from inside the event loop.

## API

| Function | Contract |
|---|---|
| `set_nonblocking(fd)` | adds `O_NONBLOCK`, keeps other flags |
| `read_available(fd, buf, max)` | read until EAGAIN / eof / error / max |
| `write_available(fd, buf)` | write until empty / EAGAIN / error; consumes what was sent |
| `connect_nonblocking(host, port)` | a socket with a connect in progress |
| `connect_error(fd)` | 0 or the connect's errno, once writable |

## Task

    bazel test //net/nonblocking:nonblocking_test
