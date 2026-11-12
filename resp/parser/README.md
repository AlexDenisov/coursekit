# resp/parser: Incremental RESP parser

> **Summary.** A server reads whatever the kernel has, which may be half a
> request or three and a half. This exercise turns that byte stream into
> commands. `server/hello_cli` and `net/client` call it on every read, and
> `persist/aof` replays a log through it. `parse_int64` comes back in
> `db/strings` for INCR. The surprising part: "not enough bytes yet" is a normal
> result, and most of the bugs live at the exact boundary between "incomplete"
> and "error".
>
> **Before:** `resp/encoder` (the same format from the writing side).
> **Next:** `server/hello_cli`, `net/client`.

## What a client sends

Every client, `redis-cli` included, sends a request as an array of bulk
strings:

```
SET k hello   ->   *3\r\n $3\r\nSET\r\n $1\r\nk\r\n $5\r\nhello\r\n      (31 bytes; spaces added here for reading)
```

`parse_request(in)` looks at the front of the client's buffer and answers one
of three things:

| Result | Meaning | What the server does |
|---|---|---|
| `kOk`, `consumed`, `args` | one whole request | run it, drop `consumed` bytes, try again |
| `kIncomplete` | a valid start of a request | wait for the next read |
| `kError`, `error` | can never become valid | reply `-<error>`, close the connection |

The parser keeps no state between calls. When a request is incomplete, the
server calls it again later with the same bytes plus the new ones. (Real Redis
keeps a little state, how many elements are left and the current bulk length,
so it doesn't rescan. That's an optimisation you can add later.)

## The algorithm

Walk a position `p` through `in`:

```
"*3\r\n$3\r\nSET\r\n$1\r\nk\r\n$5\r\nhello\r\n"
 ^ count line: find '\r' -> "3" -> parse_int64 -> 3 elements
     ^ element line: must start with '$' -> "3" -> length 3
          ^ payload: take 3 bytes "SET" without looking at them, skip 2 more
               ... twice more ...                                    consumed = 31
```

At every step, if the bytes you need aren't there yet, the answer is
`kIncomplete`. A line counts as complete once its `\r` and one byte after it
have arrived. Redis doesn't check that this byte is `\n`, and it doesn't check
the two bytes after a payload either. We match Redis exactly, because the tests
were recorded from it.

`parse_int64` is Redis's `string2ll`, and it's strict on purpose:
`"01"`, `"+1"`, `" 1"` and `"-0"` are not numbers. Accumulate the magnitude in
`uint64_t` and check the limit before every multiply-and-add. Checking after the
fact misses values that wrap all the way around (`"18446744073709551616"` wraps
to 0).

## The pitfalls

1. **Split anywhere.** A read can end in the middle of `\r\n`, in the middle of
   a length, or one byte before the end of a payload. Every proper prefix of a
   valid request must be `kIncomplete`, never an error and never a shorter
   request (`Parser.EveryPrefixIsIncomplete`).
2. **Payloads contain `\r\n`.** Use the length; never search for the next
   `\r\n` inside a payload (`Parser.PayloadMayContainCrlf`).
3. **Numbers from the network are hostile.** `*2147483647\r\n` is a legal count.
   If you `reserve()` that many strings up front, you ask for about 64 GB
   (`Parser.HugeCountDoesNotPreallocate`). `$536870913` is over the 512 MB limit
   and must be rejected as soon as its line is complete, before any payload
   arrives (`ParserErrors.BulkLengthAbove512MbIsRejectedImmediately`).
4. **Garbage without a newline.** Once more than 64 KB has arrived and the count
   line still has no `\r`, Redis gives up (`too big mbulk count string`).
   Otherwise one client could make you buffer forever. (Redis 8 has no such
   limit for the `$` line; the tests follow Redis.)

## Inline requests (optional)

If the first byte isn't `*`, Redis reads a line of text, as typed into `nc` or
`telnet`. It splits on spaces and understands quotes:

```
SET k "hello world"\r\n     ->  {"SET", "k", "hello world"}
ECHO "a\x41\n"\r\n          ->  {"ECHO", "aA\n"}           escapes in double quotes
ECHO 'a\'b'\r\n             ->  {"ECHO", "a'b"}            only \' in single quotes
ECHO "abc"def\r\n           ->  ERR Protocol error: unbalanced quotes in request
```

This is Redis's `sdssplitargs`. A small state machine works well: outside
quotes, in double quotes, in single quotes.

## API

| Function | Contract |
|---|---|
| `parse_int64(s, &out)` | `string2ll` rules; false and `out` untouched if invalid. O(\|s\|) |
| `parse_request(in)` | first request at the front of `in`; see `parser.h` for every error. O(\|in\|) |
| `parse_inline(in)` (optional) | one inline line; `parse_request` calls it when `in[0] != '*'` |

## Task

    bazel test //resp/parser:parser_test
    bazel test //resp/parser:optional_test

See it for yourself: with `redis-server` running, try
`printf '*1\r\n$4\r\nPING\r\n' | nc localhost 6379` and
`printf '*1\r\n:5\r\n' | nc localhost 6379`.
