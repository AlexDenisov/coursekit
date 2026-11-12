#include "persist/rdb_writer/rdb_writer.h"

#include <unistd.h>

#include <cstdio>
#include <cstring>
#include <limits>

#include "db/hashes/hashes.h"
#include "db/lists/lists.h"
#include "db/sets/sets.h"
#include "db/zsets/zsets.h"
#include "persist/crc64/crc64.h"
#include "resp/parser/parser.h"

namespace rd::rdb {

namespace {

void put_le(std::string& out, uint64_t v, int n) {
  for (int i = 0; i < n; ++i) out += static_cast<char>((v >> (8 * i)) & 0xff);
}

void write_type(std::string& out, const Value& v) {
  switch (v.type()) {
    case Type::kString:
      out += static_cast<char>(kTypeString);
      break;
    case Type::kList:
      out += static_cast<char>(kTypeList);
      break;
    case Type::kSet:
      out += static_cast<char>(kTypeSet);
      break;
    case Type::kHash:
      out += static_cast<char>(kTypeHash);
      break;
    case Type::kZSet:
      out += static_cast<char>(kTypeZSet2);
      break;
  }
}

void write_body(std::string& out, const Value& v) {
  switch (v.type()) {
    case Type::kString:
      write_string(out, static_cast<const StringValue&>(v).s);
      break;
    case Type::kList: {
      const auto& l = static_cast<const ListValue&>(v);
      write_length(out, l.items.size());
      for (const auto& s : l.items) write_string(out, s);
      break;
    }
    case Type::kSet: {
      const auto& s = static_cast<const SetValue&>(v);
      write_length(out, s.members.size());
      s.members.for_each([&](const std::string& m, const bool&) { write_string(out, m); });
      break;
    }
    case Type::kHash: {
      const auto& h = static_cast<const HashValue&>(v);
      write_length(out, h.fields.size());
      h.fields.for_each([&](const std::string& f, const std::string& val) {
        write_string(out, f);
        write_string(out, val);
      });
      break;
    }
    case Type::kZSet: {
      const auto& z = static_cast<const ZSetValue&>(v);
      write_length(out, z.order.size());
      for (auto* n = z.order.first(); n; n = n->next()) {
        write_string(out, n->member);
        uint64_t bits;
        std::memcpy(&bits, &n->score, 8);
        put_le(out, bits, 8);
      }
      break;
    }
  }
}

}  // namespace

void write_length(std::string& out, uint64_t n) {
  if (n < 64) {
    out += static_cast<char>(n);
  } else if (n < 16384) {
    out += static_cast<char>(0x40 | (n >> 8));
    out += static_cast<char>(n & 0xff);
  } else if (n <= std::numeric_limits<uint32_t>::max()) {
    out += '\x80';
    for (int i = 3; i >= 0; --i) out += static_cast<char>((n >> (8 * i)) & 0xff);
  } else {
    out += '\x81';
    for (int i = 7; i >= 0; --i) out += static_cast<char>((n >> (8 * i)) & 0xff);
  }
}

void write_string(std::string& out, std::string_view s) {
  int64_t v;
  if (resp::parse_int64(s, &v) && v >= std::numeric_limits<int32_t>::min() && v <= std::numeric_limits<int32_t>::max()) {
    if (v >= -128 && v <= 127) {
      out += '\xC0';
      put_le(out, static_cast<uint64_t>(v), 1);
    } else if (v >= -32768 && v <= 32767) {
      out += '\xC1';
      put_le(out, static_cast<uint64_t>(v), 2);
    } else {
      out += '\xC2';
      put_le(out, static_cast<uint64_t>(v), 4);
    }
    return;
  }
  write_length(out, s.size());
  out += s;
}

std::string write_snapshot(const Keyspace& ks) {
  std::string out = kWriteMagic;
  out += static_cast<char>(kOpAux);
  write_string(out, "redis-ver");
  write_string(out, "7.0.0");
  out += static_cast<char>(kOpSelectDb);
  write_length(out, 0);
  out += static_cast<char>(kOpResizeDb);
  write_length(out, ks.entries().size());
  write_length(out, ks.expires().size());
  ks.entries().for_each([&](const std::string& key, const Keyspace::Entry& e) {
    if (auto when = ks.get_expire(key)) {
      out += static_cast<char>(kOpExpireMs);
      put_le(out, static_cast<uint64_t>(*when), 8);
    }
    write_type(out, *e.value);
    write_string(out, key);
    write_body(out, *e.value);
  });
  out += static_cast<char>(kOpEof);
  put_le(out, crc64_fast(0, out), 8);
  return out;
}

bool save_snapshot(const Keyspace& ks, const std::string& path) {
  std::string data = write_snapshot(ks);
  std::string tmp = path + ".tmp";
  FILE* f = std::fopen(tmp.c_str(), "wb");
  if (f == nullptr) return false;
  bool ok = std::fwrite(data.data(), 1, data.size(), f) == data.size() && std::fflush(f) == 0 && ::fsync(fileno(f)) == 0;
  ok = std::fclose(f) == 0 && ok;
  if (!ok || std::rename(tmp.c_str(), path.c_str()) != 0) {
    std::remove(tmp.c_str());
    return false;
  }
  return true;
}

}  // namespace rd::rdb
