#include "db/generic/generic.h"

#include <memory>
#include <string>
#include <vector>

#include "db/glob/glob.h"
#include "ds/dict_scan/dict_scan.h"
#include "resp/encoder/encoder.h"
#include "resp/parser/parser.h"

namespace rd {

namespace {

bool eq(std::string_view a, std::string_view b) {
  if (a.size() != b.size()) return false;
  for (size_t i = 0; i < a.size(); ++i) {
    char x = a[i], y = b[i];
    if (x >= 'A' && x <= 'Z') x = static_cast<char>(x - 'A' + 'a');
    if (y >= 'A' && y <= 'Z') y = static_cast<char>(y - 'A' + 'a');
    if (x != y) return false;
  }
  return true;
}

bool parse_cursor(std::string_view s, uint64_t* out) {
  if (s.empty() || s.size() > 20) return false;
  uint64_t v = 0;
  for (char c : s) {
    if (c < '0' || c > '9') return false;
    uint64_t d = static_cast<uint64_t>(c - '0');
    if (v > (UINT64_MAX - d) / 10) return false;
    v = v * 10 + d;
  }
  *out = v;
  return true;
}

// Renames; returns false (reply written) on error.
bool do_rename(Keyspace& ks, CommandContext& c, bool nx) {
  const std::string& src = c.args[1];
  const std::string& dst = c.args[2];
  if (ks.find(src) == nullptr) {
    resp::append_error(c.reply, "ERR no such key");
    return false;
  }
  if (nx && ks.find(dst) != nullptr) {
    resp::append_integer(c.reply, 0);
    return false;
  }
  if (src == dst) return true;
  std::optional<int64_t> ttl = ks.get_expire(src);
  std::unique_ptr<Value> v = std::move(ks.entries().find(src)->value);
  ks.erase(src);
  ks.set(dst, std::move(v));
  if (ttl) ks.set_expire(dst, *ttl);
  return true;
}

}  // namespace

void register_generic_commands(CommandTable& table, Keyspace& ks, Rng& rng) {
  table.add({"keys", 2, [&ks](CommandContext& c) {
               const std::string& pattern = c.args[1];
               bool all = pattern == "*";
               std::vector<std::string> keys;
               ks.entries().for_each([&](const std::string& k, Keyspace::Entry&) {
                 if ((all || glob_match(pattern, k)) && !ks.is_expired(k)) keys.push_back(k);
               });
               resp::append_array_header(c.reply, keys.size());
               for (auto& k : keys) resp::append_bulk_string(c.reply, k);
             }});

  table.add({"scan", -2, [&ks](CommandContext& c) {
               uint64_t cursor;
               if (!parse_cursor(c.args[1], &cursor)) return resp::append_error(c.reply, "ERR invalid cursor");
               int64_t count = 10;
               std::string pattern;
               bool use_pattern = false;
               std::optional<std::string> type;
               for (size_t i = 2; i < c.args.size(); i += 2) {
                 if (i + 1 >= c.args.size()) return resp::append_error(c.reply, "ERR syntax error");
                 const std::string& opt = c.args[i];
                 if (eq(opt, "count")) {
                   if (!resp::parse_int64(c.args[i + 1], &count)) {
                     return resp::append_error(c.reply, "ERR value is not an integer or out of range");
                   }
                   if (count < 1) return resp::append_error(c.reply, "ERR syntax error");
                 } else if (eq(opt, "match")) {
                   pattern = c.args[i + 1];
                   use_pattern = pattern != "*";
                 } else if (eq(opt, "type")) {
                   type = c.args[i + 1];
                 } else {
                   return resp::append_error(c.reply, "ERR syntax error");
                 }
               }
               std::vector<std::string> keys;
               int64_t calls = 0;
               do {
                 cursor = dict_scan(ks.entries(), cursor, [&](const std::string& k, const Keyspace::Entry&) {
                   keys.push_back(k);
                 });
               } while (cursor != 0 && static_cast<int64_t>(keys.size()) < count && ++calls < count * 10);
               std::vector<std::string> out;
               for (auto& k : keys) {
                 if (use_pattern && !glob_match(pattern, k)) continue;
                 Value* v = ks.find(k);  // also deletes it if expired
                 if (v == nullptr) continue;
                 if (type && !eq(*type, type_name(v->type()))) continue;
                 out.push_back(k);
               }
               resp::append_array_header(c.reply, 2);
               resp::append_bulk_string(c.reply, std::to_string(cursor));
               resp::append_array_header(c.reply, out.size());
               for (auto& k : out) resp::append_bulk_string(c.reply, k);
             }});

  table.add({"rename", 3, [&ks](CommandContext& c) {
               if (do_rename(ks, c, false)) resp::append_simple_string(c.reply, "OK");
             }});
  table.add({"renamenx", 3, [&ks](CommandContext& c) {
               if (do_rename(ks, c, true)) resp::append_integer(c.reply, 1);
             }});

  table.add({"dbsize", 1, [&ks](CommandContext& c) { resp::append_integer(c.reply, static_cast<int64_t>(ks.size())); }});

  auto flush = [&ks](CommandContext& c) {
    if (c.args.size() > 2 || (c.args.size() == 2 && !eq(c.args[1], "async") && !eq(c.args[1], "sync"))) {
      return resp::append_error(c.reply, "ERR syntax error");
    }
    ks.clear();
    resp::append_simple_string(c.reply, "OK");
  };
  table.add({"flushdb", -1, flush});
  table.add({"flushall", -1, flush});

  table.add({"randomkey", 1, [&ks, &rng](CommandContext& c) {
               for (int tries = 0; tries < 100 && ks.size() > 0; ++tries) {
                 std::string k = *ks.entries().random_key(rng);
                 if (ks.find(k) != nullptr) return resp::append_bulk_string(c.reply, k);
               }
               resp::append_null_bulk(c.reply);
             }});
}

}  // namespace rd
