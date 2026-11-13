#pragma once

// persist/rdb_writer: snapshots in Redis's RDB format. The spec is this file; the story is
// README.md.

#include <cstdint>
#include <string>
#include <string_view>

#include "db/keyspace/keyspace.h"

namespace rd::rdb {

// Opcodes and value types (rdb.h). Given.
inline constexpr uint8_t kOpIdle = 0xF8, kOpFreq = 0xF9, kOpAux = 0xFA, kOpResizeDb = 0xFB, kOpExpireMs = 0xFC,
                         kOpExpireSec = 0xFD, kOpSelectDb = 0xFE, kOpEof = 0xFF;
inline constexpr uint8_t kTypeString = 0, kTypeList = 1, kTypeSet = 2, kTypeZSet = 3, kTypeHash = 4, kTypeZSet2 = 5,
                         kTypeSetIntset = 11, kTypeHashListpack = 16, kTypeZSetListpack = 17,
                         kTypeListQuicklist2 = 18, kTypeSetListpack = 20;
// "REDIS0011": old enough that every Redis since 7.0 loads it.
inline constexpr char kWriteMagic[] = "REDIS0011";

// RDB's length encoding:
//   n < 64           1 byte:  00nnnnnn
//   n < 16384        2 bytes: 01nnnnnn nnnnnnnn          (big-endian)
//   n < 2^32         0x80, then 4 bytes big-endian
//   otherwise        0x81, then 8 bytes big-endian
void write_length(std::string& out, uint64_t n);

// A string. If it passes resp::parse_int64 and fits in int32, Redis stores the number:
// 0xC0 + 1 byte, 0xC1 + 2 bytes, or 0xC2 + 4 bytes (little-endian, smallest that fits).
// Otherwise its length, then its bytes.
void write_string(std::string& out, std::string_view s);

// A whole snapshot of ks:
//   kWriteMagic
//   kOpAux "redis-ver" "<anything>" (optional; one or more aux fields)
//   kOpSelectDb 0;  kOpResizeDb <keys> <keys with a TTL>
//   per key: [kOpExpireMs <8 bytes LE, unix ms>] <type> <key string> <value>
//     string -> kTypeString: the string
//     list   -> kTypeList:   length, then each element (head to tail)
//     set    -> kTypeSet:    length, then each member
//     hash   -> kTypeHash:   length (fields), then field, value, field, value...
//     zset   -> kTypeZSet2:  length, then member, score as 8-byte LE IEEE double, ...
//   kOpEof, then the CRC-64 (persist/crc64) of every byte before it, 8 bytes LE.
// Every key is written, expired or not (loaders skip expired keys).
std::string write_snapshot(const Keyspace& ks);

// write_snapshot into `path`, atomically: write `path`.tmp, then rename it over path, so
// a crash mid-write never leaves a half-written dump.rdb. False on any I/O error.
bool save_snapshot(const Keyspace& ks, const std::string& path);

}  // namespace rd::rdb
