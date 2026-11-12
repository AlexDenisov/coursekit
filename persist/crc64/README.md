# persist/crc64: CRC-64 (Jones)

> **Summary.** Every RDB file ends with an 8-byte CRC-64 of everything before
> it, and so does every `DUMP` payload. `persist/rdb_writer` writes it and
> `persist/rdb_reader` refuses a file whose checksum doesn't match. The
> surprising part: a CRC is long division, with xor instead of subtraction.
>
> **Before:** nothing. **Next:** `persist/rdb_writer`.

## Division without carries

Treat the message as one huge binary number and divide it by a fixed 65-bit
polynomial using xor instead of subtraction. The 64-bit remainder is the CRC.
Flip any one bit, or any burst of up to 64 bits, and the remainder changes.

Redis uses the "Jones" polynomial `0xad93d23594c935a9`, **reflected**: each
byte is fed least significant bit first. That makes the loop shift *right* and
use the bit-reversed polynomial `0x95ac9329ac4bc9b5`:

```
crc ^= byte
repeat 8 times:
    crc = (crc & 1) ? (crc >> 1) ^ 0x95ac9329ac4bc9b5 : crc >> 1
```

The initial value is 0 and there's no final xor. The published check value is
`crc64(0, "123456789") = 0xe9c6d914c4b8d9ca`.

## Two things to notice

- **Streaming.** Since `crc` carries the whole state, you can checksum a file
  as you write it, in pieces: `crc64(crc64(0, a), b) == crc64(0, a + b)`.
- **Zeros are invisible from 0.** With initial value 0, leading zero bytes
  don't change the CRC (`Crc64.ZerosFromZeroStayZero`). Many CRC variants
  start from all ones for exactly that reason. Redis's doesn't, so it doesn't
  matter here.

## The test vectors come from Redis

`tools/crc64_reference.py` computes values from the definition and checks them
against real Redis: `DUMP key` returns a payload whose last 8 bytes are the
CRC-64 (little-endian) of the rest. `Crc64.MatchesRedisDumpChecksums` uses
those payloads.

## Optional: table-driven

Eight shift-and-xor steps per byte are slow. All eight depend only on the low
byte of `crc ^ byte`, so precompute `table[256]` once and process a byte per
lookup: `crc = table[(crc ^ b) & 0xff] ^ (crc >> 8)`. Redis goes further,
processing 8 bytes at a time, but one table is enough here.

## API

| Function | Contract |
|---|---|
| `crc64(crc, data)` | bit by bit, continuing from `crc` |
| `crc64_fast(crc, data)` (optional) | same results from a 256-entry table; needed for speed by `persist/rdb_writer` |

## Task

    bazel test //persist/crc64:crc64_test
    bazel test //persist/crc64:optional_test
