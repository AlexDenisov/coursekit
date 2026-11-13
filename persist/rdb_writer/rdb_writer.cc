#include "persist/rdb_writer/rdb_writer.h"

#include <cstdio>
#include <cstring>

#include "db/hashes/hashes.h"
#include "db/lists/lists.h"
#include "db/sets/sets.h"
#include "db/zsets/zsets.h"
#include "persist/crc64/crc64.h"
#include "resp/parser/parser.h"

namespace rd::rdb {

void write_length(std::string& out, uint64_t n) {
  // TODO(you): the four cases.
}

void write_string(std::string& out, std::string_view s) {
  // TODO(you): integer encoding when it applies, else length + bytes.
}

std::string write_snapshot(const Keyspace& ks) {
  // TODO(you): header, SELECTDB, RESIZEDB, every entry (switch on value->type() and
  // static_cast to ListValue, SetValue, HashValue, ZSetValue), EOF, CRC.
  return "";
}

bool save_snapshot(const Keyspace& ks, const std::string& path) {
  // TODO(you): fopen path + ".tmp", fwrite, fflush, fsync(fileno(f)), fclose, rename.
  return false;
}

}  // namespace rd::rdb
