#include "db/strings/strings.h"

#include <cstdint>
#include <limits>
#include <optional>

#include "common/check.h"
#include "resp/encoder/encoder.h"
#include "resp/parser/parser.h"

namespace rd {

namespace {

constexpr char kNotInteger[] = "ERR value is not an integer or out of range";
constexpr char kSyntax[] = "ERR syntax error";
constexpr int64_t kMaxStringSize = 512 * 1024 * 1024;

bool equals_ignore_case(std::string_view a, std::string_view b) {
  if (a.size() != b.size()) return false;
  for (size_t i = 0; i < a.size(); ++i) {
    char x = a[i], y = b[i];
    if (x >= 'A' && x <= 'Z') x = static_cast<char>(x - 'A' + 'a');
    if (y >= 'A' && y <= 'Z') y = static_cast<char>(y - 'A' + 'a');
    if (x != y) return false;
  }
  return true;
}

const std::string* lookup(StringDb& db, const std::string& key) {
  auto it = db.map.find(key);
  return it == db.map.end() ? nullptr : &it->second;
}

void incr_by(StringDb& db, CommandContext& c, int64_t by) {
  const std::string& key = c.args[1];
  int64_t old = 0;
  if (const std::string* v = lookup(db, key); v != nullptr && !resp::parse_int64(*v, &old)) {
    resp::append_error(c.reply, kNotInteger);
    return;
  }
  if ((by > 0 && old > std::numeric_limits<int64_t>::max() - by) ||
      (by < 0 && old < std::numeric_limits<int64_t>::min() - by)) {
    resp::append_error(c.reply, "ERR increment or decrement would overflow");
    return;
  }
  int64_t result = old + by;
  db.map[key] = std::to_string(result);
  resp::append_integer(c.reply, result);
}

std::optional<int64_t> int_arg(CommandContext& c, size_t i) {
  int64_t v;
  if (!resp::parse_int64(c.args[i], &v)) {
    resp::append_error(c.reply, kNotInteger);
    return std::nullopt;
  }
  return v;
}

}  // namespace

void register_string_commands(CommandTable& table, StringDb& db) {
  table.add({"get", 2, [&db](CommandContext& c) {
               if (const std::string* v = lookup(db, c.args[1])) {
                 resp::append_bulk_string(c.reply, *v);
               } else {
                 resp::append_null_bulk(c.reply);
               }
             }});

  table.add({"set", -3, [&db](CommandContext& c) {
               bool nx = false, xx = false, get = false;
               for (size_t i = 3; i < c.args.size(); ++i) {
                 const std::string& opt = c.args[i];
                 if (equals_ignore_case(opt, "nx") && !xx) {
                   nx = true;
                 } else if (equals_ignore_case(opt, "xx") && !nx) {
                   xx = true;
                 } else if (equals_ignore_case(opt, "get")) {
                   get = true;
                 } else {
                   resp::append_error(c.reply, kSyntax);
                   return;
                 }
               }
               const std::string& key = c.args[1];
               const std::string* old = lookup(db, key);
               std::optional<std::string> old_value;
               if (old != nullptr) old_value = *old;
               bool skip = (nx && old != nullptr) || (xx && old == nullptr);
               if (!skip) db.map[key] = c.args[2];
               if (get) {
                 if (old_value) {
                   resp::append_bulk_string(c.reply, *old_value);
                 } else {
                   resp::append_null_bulk(c.reply);
                 }
               } else if (skip) {
                 resp::append_null_bulk(c.reply);
               } else {
                 resp::append_simple_string(c.reply, "OK");
               }
             }});

  table.add({"del", -2, [&db](CommandContext& c) {
               int64_t removed = 0;
               for (size_t i = 1; i < c.args.size(); ++i) removed += db.map.erase(c.args[i]);
               resp::append_integer(c.reply, removed);
             }});

  table.add({"exists", -2, [&db](CommandContext& c) {
               int64_t n = 0;
               for (size_t i = 1; i < c.args.size(); ++i) n += db.map.count(c.args[i]);
               resp::append_integer(c.reply, n);
             }});

  table.add({"incr", 2, [&db](CommandContext& c) { incr_by(db, c, 1); }});
  table.add({"decr", 2, [&db](CommandContext& c) { incr_by(db, c, -1); }});
  table.add({"incrby", 3, [&db](CommandContext& c) {
               if (auto by = int_arg(c, 2)) incr_by(db, c, *by);
             }});
  table.add({"decrby", 3, [&db](CommandContext& c) {
               auto by = int_arg(c, 2);
               if (!by) return;
               if (*by == std::numeric_limits<int64_t>::min()) {
                 resp::append_error(c.reply, "ERR decrement would overflow");
                 return;
               }
               incr_by(db, c, -*by);
             }});

  table.add({"append", 3, [&db](CommandContext& c) {
               std::string& v = db.map[c.args[1]];
               v += c.args[2];
               resp::append_integer(c.reply, static_cast<int64_t>(v.size()));
             }});

  table.add({"strlen", 2, [&db](CommandContext& c) {
               const std::string* v = lookup(db, c.args[1]);
               resp::append_integer(c.reply, v ? static_cast<int64_t>(v->size()) : 0);
             }});

  table.add({"mget", -2, [&db](CommandContext& c) {
               resp::append_array_header(c.reply, c.args.size() - 1);
               for (size_t i = 1; i < c.args.size(); ++i) {
                 if (const std::string* v = lookup(db, c.args[i])) {
                   resp::append_bulk_string(c.reply, *v);
                 } else {
                   resp::append_null_bulk(c.reply);
                 }
               }
             }});

  table.add({"mset", -3, [&db](CommandContext& c) {
               if (c.args.size() % 2 == 0) {
                 resp::append_error(c.reply, arity_error("mset"));
                 return;
               }
               for (size_t i = 1; i < c.args.size(); i += 2) db.map[c.args[i]] = c.args[i + 1];
               resp::append_simple_string(c.reply, "OK");
             }});
}

void register_string_range_commands(CommandTable& table, StringDb& db) {
  table.add({"getrange", 4, [&db](CommandContext& c) {
               auto start = int_arg(c, 2);
               if (!start) return;
               auto end = int_arg(c, 3);
               if (!end) return;
               const std::string* v = lookup(db, c.args[1]);
               int64_t s = *start, e = *end;
               if (v == nullptr || (s < 0 && e < 0 && s > e)) {
                 resp::append_bulk_string(c.reply, "");
                 return;
               }
               auto len = static_cast<int64_t>(v->size());
               if (s < 0) s += len;
               if (e < 0) e += len;
               if (s < 0) s = 0;
               if (e < 0) e = 0;
               if (e >= len) e = len - 1;
               if (len == 0 || s > e) {
                 resp::append_bulk_string(c.reply, "");
                 return;
               }
               resp::append_bulk_string(
                   c.reply, std::string_view(*v).substr(static_cast<size_t>(s), static_cast<size_t>(e - s + 1)));
             }});

  table.add({"setrange", 4, [&db](CommandContext& c) {
               auto offset = int_arg(c, 2);
               if (!offset) return;
               if (*offset < 0) {
                 resp::append_error(c.reply, "ERR offset is out of range");
                 return;
               }
               const std::string& value = c.args[3];
               auto it = db.map.find(c.args[1]);
               if (value.empty()) {
                 resp::append_integer(c.reply, it == db.map.end() ? 0 : static_cast<int64_t>(it->second.size()));
                 return;
               }
               if (*offset > kMaxStringSize - static_cast<int64_t>(value.size())) {
                 resp::append_error(c.reply, "ERR string exceeds maximum allowed size (proto-max-bulk-len)");
                 return;
               }
               std::string& v = db.map[c.args[1]];
               auto off = static_cast<size_t>(*offset);
               if (v.size() < off + value.size()) v.resize(off + value.size(), '\0');
               v.replace(off, value.size(), value);
               resp::append_integer(c.reply, static_cast<int64_t>(v.size()));
             }});
}

}  // namespace rd
