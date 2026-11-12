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

namespace {

class Cursor {
 public:
  explicit Cursor(std::string_view d) : d_(d) {}
  size_t pos() const { return i_; }
  bool ok() const { return ok_; }
  std::string error() const { return error_; }
  void fail(const std::string& why) {
    if (ok_) error_ = why + " at offset " + std::to_string(i_);
    ok_ = false;
  }

  int byte() {
    if (!need(1)) return -1;
    return static_cast<unsigned char>(d_[i_++]);
  }
  uint64_t le(int n) {
    if (!need(static_cast<size_t>(n))) return 0;
    uint64_t v = 0;
    for (int k = n - 1; k >= 0; --k) v = (v << 8) | static_cast<unsigned char>(d_[i_ + static_cast<size_t>(k)]);
    i_ += static_cast<size_t>(n);
    return v;
  }
  // A length; *special is set for the 11xxxxxx string encodings.
  uint64_t length(bool* special = nullptr) {
    int b = byte();
    if (b < 0) return 0;
    if (special) *special = false;
    switch (b >> 6) {
      case 0: return static_cast<uint64_t>(b & 63);
      case 1: {
        int c = byte();
        return c < 0 ? 0 : static_cast<uint64_t>(((b & 63) << 8) | c);
      }
      case 3:
        if (special) {
          *special = true;
        } else {
          fail("unexpected string encoding in a length");
        }
        return static_cast<uint64_t>(b & 63);
    }
    int n = b == 0x80 ? 4 : b == 0x81 ? 8 : 0;
    if (n == 0) {
      fail("bad length byte");
      return 0;
    }
    if (!need(static_cast<size_t>(n))) return 0;
    uint64_t v = 0;
    for (int k = 0; k < n; ++k) v = (v << 8) | static_cast<unsigned char>(d_[i_++]);
    return v;
  }
  std::string string() {
    bool special = false;
    uint64_t n = length(&special);
    if (!ok_) return "";
    if (special) {
      if (n <= 2) {
        int w = n == 0 ? 1 : n == 1 ? 2 : 4;
        uint64_t u = le(w);
        int shift = 64 - 8 * w;
        return std::to_string(static_cast<int64_t>(u << shift) >> shift);
      }
      if (n == 3) {
        uint64_t clen = length(), ulen = length();
        if (!need(clen)) return "";
        auto out = lzf_decompress(d_.substr(i_, clen), ulen);
        i_ += clen;
        if (!out) {
          fail("bad or unsupported LZF string");
          return "";
        }
        return *out;
      }
      fail("unknown string encoding");
      return "";
    }
    if (!need(n)) return "";
    std::string s(d_.substr(i_, n));
    i_ += n;
    return s;
  }

 private:
  bool need(size_t n) {
    if (ok_ && i_ + n <= d_.size()) return true;
    fail("truncated");
    return false;
  }
  std::string_view d_;
  size_t i_ = 0;
  bool ok_ = true;
  std::string error_;
};

std::unique_ptr<Value> read_value(Cursor& c, int type) {
  switch (type) {
    case kTypeString:
      return std::make_unique<StringValue>(c.string());
    case kTypeList: {
      auto l = std::make_unique<ListValue>();
      for (uint64_t n = c.length(); n-- > 0 && c.ok();) l->items.push_back(c.string());
      return l;
    }
    case kTypeListQuicklist2: {
      auto l = std::make_unique<ListValue>();
      for (uint64_t nodes = c.length(); nodes-- > 0 && c.ok();) {
        uint64_t container = c.length();
        std::string blob = c.string();
        if (container == 1) {
          l->items.push_back(std::move(blob));
        } else if (container == 2) {
          auto lp = Listpack::from_bytes(blob);
          if (!lp) {
            c.fail("corrupt listpack");
            return nullptr;
          }
          for (auto& e : lp->to_vector()) l->items.push_back(std::move(e));
        } else {
          c.fail("unknown quicklist container");
          return nullptr;
        }
      }
      return l;
    }
    case kTypeSet: {
      auto s = std::make_unique<SetValue>();
      for (uint64_t n = c.length(); n-- > 0 && c.ok();) s->members.insert(c.string(), true);
      return s;
    }
    case kTypeSetIntset: {
      auto is = IntSet::from_bytes(c.string());
      if (!is) {
        c.fail("corrupt intset");
        return nullptr;
      }
      auto s = std::make_unique<SetValue>();
      for (size_t i = 0; i < is->size(); ++i) s->members.insert(std::to_string(is->at(i)), true);
      return s;
    }
    case kTypeSetListpack: {
      auto lp = Listpack::from_bytes(c.string());
      if (!lp) {
        c.fail("corrupt listpack");
        return nullptr;
      }
      auto s = std::make_unique<SetValue>();
      for (auto& m : lp->to_vector()) s->members.insert(std::move(m), true);
      return s;
    }
    case kTypeHash: {
      auto h = std::make_unique<HashValue>();
      for (uint64_t n = c.length(); n-- > 0 && c.ok();) {
        std::string f = c.string();
        std::string v = c.string();
        h->fields.insert(std::move(f), std::move(v));
      }
      return h;
    }
    case kTypeHashListpack: {
      auto lp = Listpack::from_bytes(c.string());
      if (!lp) {
        c.fail("corrupt listpack");
        return nullptr;
      }
      auto all = lp->to_vector();
      auto h = std::make_unique<HashValue>();
      for (size_t i = 0; i + 1 < all.size(); i += 2) h->fields.insert(all[i], all[i + 1]);
      return h;
    }
    case kTypeZSet2: {
      auto z = std::make_unique<ZSetValue>();
      for (uint64_t n = c.length(); n-- > 0 && c.ok();) {
        std::string m = c.string();
        uint64_t bits = c.le(8);
        double s;
        std::memcpy(&s, &bits, 8);
        z->set(m, s);
      }
      return z;
    }
    case kTypeZSetListpack: {
      auto lp = Listpack::from_bytes(c.string());
      if (!lp) {
        c.fail("corrupt listpack");
        return nullptr;
      }
      auto all = lp->to_vector();
      auto z = std::make_unique<ZSetValue>();
      for (size_t i = 0; i + 1 < all.size(); i += 2) z->set(all[i], std::strtod(all[i + 1].c_str(), nullptr));
      return z;
    }
  }
  c.fail("unknown value type " + std::to_string(type));
  return nullptr;
}

}  // namespace

LoadResult load_snapshot(std::string_view data, Keyspace& ks) {
  LoadResult r;
  if (data.size() < 9 || data.substr(0, 5) != "REDIS") {
    r.error = "not an RDB file";
    return r;
  }
  int version = std::atoi(std::string(data.substr(5, 4)).c_str());
  if (version < 1 || version > 15) {
    r.error = "unsupported RDB version " + std::to_string(version);
    return r;
  }
  Cursor c(data);
  c.le(4);
  c.le(5);  // skip the header
  uint64_t db = 0;
  std::optional<int64_t> expire;
  int64_t now = ks.clock().now_ms();
  while (c.ok()) {
    int op = c.byte();
    if (op < 0) break;
    if (op == kOpAux) {
      c.string();
      c.string();
    } else if (op == kOpSelectDb) {
      db = c.length();
    } else if (op == kOpResizeDb) {
      c.length();
      c.length();
    } else if (op == kOpExpireMs) {
      expire = static_cast<int64_t>(c.le(8));
    } else if (op == kOpExpireSec) {
      expire = static_cast<int64_t>(c.le(4)) * 1000;
    } else if (op == kOpIdle) {
      c.length();
    } else if (op == kOpFreq) {
      c.byte();
    } else if (op == kOpEof) {
      size_t covered = c.pos();
      uint64_t stored = c.le(8);
      if (!c.ok()) break;
      if (stored != 0 && stored != crc64_fast(0, data.substr(0, covered))) {
        r.error = "checksum mismatch";
        return r;
      }
      r.ok = true;
      return r;
    } else {
      std::string key = c.string();
      std::unique_ptr<Value> v = read_value(c, op);
      if (!c.ok()) break;
      if (db != 0) {
        expire.reset();
        continue;
      }
      if (expire && *expire <= now) {
        ++r.expired_skipped;
      } else {
        ks.set(key, std::move(v));
        if (expire) ks.set_expire(key, *expire);
        ++r.keys_loaded;
      }
      expire.reset();
    }
  }
  r.error = c.ok() ? "truncated: no EOF" : c.error();
  return r;
}

LoadResult load_snapshot_file(const std::string& path, Keyspace& ks) {
  std::ifstream f(path, std::ios::binary);
  if (!f) {
    LoadResult r;
    r.error = "can't open " + path;
    return r;
  }
  std::stringstream ss;
  ss << f.rdbuf();
  std::string data = ss.str();
  return load_snapshot(data, ks);
}

std::optional<std::string> lzf_decompress(std::string_view in, size_t out_len) {
  std::string out;
  out.reserve(out_len);
  size_t i = 0;
  while (i < in.size()) {
    unsigned ctrl = static_cast<unsigned char>(in[i++]);
    if (ctrl < 32) {
      size_t n = ctrl + 1;
      if (i + n > in.size() || out.size() + n > out_len) return std::nullopt;
      out.append(in.substr(i, n));
      i += n;
    } else {
      size_t len = ctrl >> 5;
      if (len == 7) {
        if (i >= in.size()) return std::nullopt;
        len += static_cast<unsigned char>(in[i++]);
      }
      if (i >= in.size()) return std::nullopt;
      size_t back = ((ctrl & 31) << 8) + static_cast<unsigned char>(in[i++]) + 1;
      if (back > out.size() || out.size() + len + 2 > out_len) return std::nullopt;
      size_t from = out.size() - back;
      for (size_t k = 0; k < len + 2; ++k) out += out[from + k];
    }
  }
  if (out.size() != out_len) return std::nullopt;
  return out;
}

}  // namespace rd::rdb
