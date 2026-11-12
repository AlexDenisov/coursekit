# net/buffers: I/O buffers

> **Summary.** Every client has two byte queues: the *query buffer* (bytes
> read, not yet parsed) and the *reply buffer* (replies built, not yet sent).
> `net/nonblocking` reads and writes through them, and `net/client` owns one of
> each. The surprising part: the obvious `std::string::erase(0, n)` after each
> request makes pipelining quadratic.
>
> **Before:** `net/sockets` (short reads are why buffers exist).
> **Next:** `net/nonblocking`, `net/client`.

## A queue over one allocation

```
data_:  [ consumed | live bytes            | free           ]
         0         start_                  end_             capacity
```

- `append` / `prepare` + `commit` add at `end_`. `prepare(n)` hands out a
  pointer so `read()` can write straight into the buffer, with no temporary.
- `consume(n)` moves `start_` forward. Nothing is copied.
- When everything is consumed, rewind both ends to 0. That's free, and the
  common case for a client that sends one request and waits.

## The pitfall: moving bytes on every consume

`redis-benchmark -P 16` sends 16 requests per packet. A big batch from
`redis-cli --pipe` can be megabytes. If `consume` does
`erase(0, n)`, each request moves everything behind it: 30 000 requests in
1 MB means about 15 GB copied. `Buffer.ConsumingFromTheFrontIsAmortised`
would never finish.

The fix is to compact only when it pays for itself. When the tail is full,
slide the live bytes to the front *if the dead prefix is at least as large as
the live part*. Every byte moved is then matched by a byte that was consumed,
so the total moved stays at most the total written (`bytes_moved()` checks
exactly this). Otherwise, grow the allocation, at least doubling it.

## API

| Function | Contract |
|---|---|
| `size()`, `empty()`, `view()` | the live bytes |
| `append(data)` | add at the back |
| `prepare(n)` → `char*`, `commit(k)` | write in place, then publish `k <= n` bytes |
| `consume(n)` | drop from the front, amortised O(1) |
| `clear()` | drop everything, keep the memory |
| `capacity()`, `bytes_moved()` | for the tests: memory and compaction cost |

## Task

    bazel test //net/buffers:buffer_test
