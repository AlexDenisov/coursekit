#pragma once

// persist/rdb_reader: loading RDB files, including those real Redis writes. The spec is
// this file; the story is README.md.

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>

#include "db/keyspace/keyspace.h"

namespace rd::rdb {

struct LoadResult {
  bool ok = false;
  std::string error;           // when !ok: what went wrong (contains "checksum" for a CRC mismatch)
  size_t keys_loaded = 0;
  size_t expired_skipped = 0;  // keys whose TTL had already passed (by ks.clock())
};

// Loads an RDB file's keys into ks (on top of whatever is there).
//   Header: "REDIS" + 4 digits, version 1..15. Else an error.
//   Opcodes: AUX (skip both strings), SELECTDB (keys in a db other than 0 are skipped),
//   RESIZEDB (two lengths, ignore), EXPIRETIME_MS (8 bytes LE), EXPIRETIME (4 bytes LE,
//   seconds), IDLE (a length, ignore), FREQ (1 byte, ignore), EOF.
//   Value types:
//     0 string;  1 list (length + elements);  2 set;  4 hash (field/value pairs);
//     5 zset2 (member + 8-byte LE double);  11 set as intset blob (ds/intset);
//     16 hash as listpack blob;  17 zset as listpack blob (member, score, ...; scores as
//     integers or decimal strings);  18 list as quicklist2: a length (nodes), then per
//     node a length (1 = plain: the string is one element; 2 = packed: a listpack blob)
//     and a string;  20 set as listpack blob.
//   Strings: a length + bytes, or an integer encoding (0xC0/C1/C2: 1/2/4 bytes LE, a
//   decimal string), or LZF (0xC3: compressed length, uncompressed length, data; see
//   lzf_decompress).
//   After EOF: an 8-byte LE CRC-64 of everything before it; 0 means "not computed"; a
//   mismatch is an error.
// Anything else (unknown opcode or type, truncation, a corrupt blob) is an error. Keys
// whose TTL has passed are skipped. On error, ks may hold part of the file.
LoadResult load_snapshot(std::string_view data, Keyspace& ks);
LoadResult load_snapshot_file(const std::string& path, Keyspace& ks);

// Optional. Redis's LZF decompression (lzf_d.c): a sequence of
//   ctrl < 32:   a literal run: copy the next ctrl + 1 bytes
//   ctrl >= 32:  a back-reference: len = ctrl >> 5 (if 7, add the next byte), then
//                offset = ((ctrl & 31) << 8) + next byte + 1 bytes back from the end of
//                the output; copy len + 2 bytes from there, one at a time (they may
//                overlap what's being written).
// nullopt if the input is malformed or doesn't produce exactly out_len bytes. Needed for
// files saved with rdbcompression yes (Redis's default).
std::optional<std::string> lzf_decompress(std::string_view in, size_t out_len);

}  // namespace rd::rdb
