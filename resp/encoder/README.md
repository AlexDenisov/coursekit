# resp/encoder: RESP encoder

> **Summary.** Every reply your server sends is RESP: the REdis Serialization
> Protocol. `cmd/dispatch` and every command after it write replies with these
> functions, and `net/client` sends them. The optional `append_command` is used
> again by `persist/aof` and `repl/replica`. The surprising part: you can't
> print `INT64_MIN` by printing `'-'` and then the digits of `-v`.
>
> **Before:** nothing. **Next:** `resp/parser` (the other direction),
> `cmd/dispatch`.

## Five types, one line each

RESP2 is line-based and text-prefixed. The first byte says the type, and every
header line ends in `\r\n`:

| Type | Bytes | Redis sends it for |
|---|---|---|
| simple string | `+OK\r\n` | short status replies that can't contain CR/LF |
| error | `-ERR syntax error\r\n` | any failure; the first word is the code |
| integer | `:42\r\n` | counts, INCR results, booleans as 0/1 |
| bulk string | `$5\r\nhello\r\n` | any value: binary safe, length-prefixed |
| array | `*2\r\n` + 2 more replies | multi-value replies, nested freely |

There are also two nulls: `$-1\r\n` (a missing value: `GET nokey`) and `*-1\r\n`
(a missing array: a `BLPOP` that timed out). An empty bulk string `$0\r\n\r\n`
is not null: it's the empty string.

```
MGET s missing     ->   *2\r\n          an array of two replies
                        $5\r\nhello\r\n   the value of s
                        $-1\r\n           nothing at "missing"
```

## Why bulk strings carry a length

A reader of `$4\r\na\r\nb\r\n` reads the length 4 and then takes exactly 4
bytes, `a\r\nb`, without looking at them. That's what makes values binary safe:
images, serialized objects and strings full of `\r\n` all pass through
unchanged. Simple strings and errors have no length, so they can't contain
CR or LF. For simple strings that's a precondition (checked). For errors, Redis
replaces each CR or LF with a space, because error messages often quote user
input:

```
GET with a command name of "foo\r\nbar1"
  ->  -ERR unknown command 'foo  bar1'\r\n
```

Without that, a client could get your server to send `-ERR unknown command 'foo`
followed by a second, injected reply.

## The pitfall: INT64_MIN

Integers appear in `:`, `$` and `*` lines, so you'll write them on every reply.
Redis has its own `ll2string` for speed, and you write yours too:
`format_int64`. The obvious approach is `if (v < 0) { put '-'; v = -v; }`, then
emit digits. But int64 runs from -9223372036854775808 to 9223372036854775807,
so `-INT64_MIN` doesn't fit. In C++ that's undefined behaviour, and in practice
it stays negative and your digit loop prints garbage.

The fix is to take the magnitude as `uint64_t`: `0 - static_cast<uint64_t>(v)`
is well defined and gives 9223372036854775808. Test:
`FormatInt64.Int64MinDoesNotOverflow`.

```
v = -120:  magnitude 120  ->  digits backwards "021"  ->  "-" + "120"
```

## API

| Function | Contract |
|---|---|
| `format_int64(v, buf)` | decimal digits into `buf` (room for 20), returns the length; no snprintf/to_chars |
| `append_simple_string(out, s)` | `+s\r\n`; `s` has no CR/LF (checked) |
| `append_error(out, msg)` | `-msg\r\n`, CR/LF in `msg` become spaces |
| `append_integer(out, v)` | `:v\r\n` |
| `append_bulk_string(out, s)` | `$len\r\ns\r\n`, binary safe |
| `append_null_bulk(out)` | `$-1\r\n` |
| `append_array_header(out, n)` | `*n\r\n`, then append n replies |
| `append_null_array(out)` | `*-1\r\n` |
| `append_command(out, args)` (optional) | a request: array of bulk strings. Needed by `persist/aof`, `repl/replica` |

Everything appends; nothing clears `out`.

## Task

    bazel test //resp/encoder:encoder_test
    bazel test //resp/encoder:optional_test
