#include "persist/rdb_reader/rdb_reader.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <memory>
#include <sstream>

#include "db/hashes/hashes.h"
#include "db/lists/lists.h"
#include "db/sets/sets.h"
#include "db/zsets/zsets.h"
#include "ds/intset/intset.h"
#include "ds/listpack/listpack.h"
#include "persist/crc64/crc64.h"
#include "persist/rdb_writer/rdb_writer.h"

namespace rd::rdb {

LoadResult load_snapshot(std::string_view data, Keyspace& ks) {
  // TODO(you): a small cursor class (read a byte, a length, a string, N bytes LE) whose
  // methods report truncation; then the opcode loop; then one function per value type
  // that builds the Value. Check the CRC when you reach EOF.
  return {};
}

LoadResult load_snapshot_file(const std::string& path, Keyspace& ks) {
  // TODO(you): read the whole file, then load_snapshot.
  return {};
}

std::optional<std::string> lzf_decompress(std::string_view in, size_t out_len) {
  // TODO(you, optional): as in rdb_reader.h.
  return std::nullopt;
}

}  // namespace rd::rdb
