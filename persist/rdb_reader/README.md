# persist/rdb_reader: RDB reader

> **Summary.** The other half of persistence: at startup, read `dump.rdb`
> back into the keyspace. Your writer uses the simple legacy encodings, but
> the files real Redis 8 writes use listpacks, intsets, quicklists and LZF
> compression, and this reader loads those too: the fixtures in `testdata/`
> were saved by Redis 8.10. `repl/replica` uses it to load the snapshot a
> primary sends, and `server/durable_redis` loads at startup. The surprising
> part: most of the work was already done in `ds/listpack` and `ds/intset`.
> Their blobs sit in the file unchanged.
>
> **Before:** `persist/rdb_writer` (format and round-trip), `ds/listpack`,
> `ds/intset`, `persist/crc64`. **Next:** `repl/replica`, `server/durable_redis`.

## What Redis 8 writes

`tools/rdb_reference.py` builds a dataset in real Redis and saves it. Walking
the file shows what you'll meet:

| Key | Type byte | Body |
|---|---|---|
| `str`, `int`, ... | 0 | a string (possibly int-encoded `C0`–`C2`, or LZF `C3`) |
| `hash` (2 fields) | 16 | one string: a listpack `f1 v1 f2 2` |
| `bighash` (600 fields) | 4 | 600 field/value string pairs |
| `intset` | 11 | one string: an intset blob |
| `lpset` / `bigset` | 20 / 2 | a listpack blob / a list of strings |
| `zset` / `bigzset` | 17 / 5 | a listpack (`member score ...`) / members with binary doubles |
| `list`, `biglist` | 18 | quicklist: N nodes, each `2` + a listpack (or `1` + one plain element) |

Plus aux fields (`redis-ver`, `ctime`, `used-mem`, ...), `SELECTDB`,
`RESIZEDB`, and `FC` expiry opcodes before keys that have a TTL.

## Robustness

A loader reads untrusted bytes. Every read must check that the bytes exist
(`RdbReader.TruncatedFileIsAnErrorNotACrash` cuts the fixture at several
places), every blob must be validated (`Listpack::from_bytes`,
`IntSet::from_bytes`), and the CRC at the end must match. A flipped byte
inside a 1000-byte string parses perfectly, and only the checksum notices
(`RdbReader.DetectsCorruptionWithTheChecksum`). A stored CRC of 0 means "not
computed" (`rdbchecksum no`).

A key whose TTL has passed is skipped at load time, not loaded and then
expired (`RdbReader.SkipsKeysThatHaveExpired`).

## Optional: LZF

With Redis's default `rdbcompression yes`, strings longer than 20 bytes are
LZF-compressed when that helps: `C3`, compressed length, uncompressed length,
data. LZF is a tiny LZ77: literal runs and back-references into the output so
far, which may overlap what's being written (copy one byte at a time). The
optional test loads `redis8_lzf.rdb`, the same dataset compressed.

## API

| Function | Contract |
|---|---|
| `load_snapshot(data, ks)` | everything above; `LoadResult{ok, error, keys_loaded, expired_skipped}` |
| `load_snapshot_file(path, ks)` | the same from disk |
| `lzf_decompress(in, out_len)` (optional) | Redis's `lzf_d` |

## Task

    bazel test //persist/rdb_reader:rdb_reader_test
    bazel test //persist/rdb_reader:optional_test
