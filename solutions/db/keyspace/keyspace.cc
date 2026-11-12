#include "db/keyspace/keyspace.h"

#include <limits>

#include "resp/encoder/encoder.h"
#include "resp/parser/parser.h"

namespace rd {

namespace {

constexpr char kNotInteger[] = "ERR value is not an integer or out of range";
constexpr char kSyntax[] = "ERR syntax error";

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

void wrong_type(CommandContext& c) { resp::append_error(c.reply, kWrongTypeError); }

void incr_by(Keyspace& ks, CommandContext& c, int64_t by) {
  auto [sv, wrong] = lookup<StringValue>(ks, c.args[1], true);
  if (wrong) return wrong_type(c);
  int64_t old = 0;
  if (sv != nullptr && !resp::parse_int64(sv->s, &old)) {
    resp::append_error(c.reply, kNotInteger);
    return;
  }
  if ((by > 0 && old > std::numeric_limits<int64_t>::max() - by) ||
      (by < 0 && old < std::numeric_limits<int64_t>::min() - by)) {
    resp::append_error(c.reply, "ERR increment or decrement would overflow");
    return;
  }
  int64_t result = old + by;
  if (sv != nullptr) {
    sv->s = std::to_string(result);
    sv->raw = false;
  } else {
    ks.set(c.args[1], std::make_unique<StringValue>(std::to_string(result)));
  }
  resp::append_integer(c.reply, result);
}

}  // namespace

std::string_view StringValue::encoding() const {
  if (raw) return "raw";
  int64_t v;
  if (resp::parse_int64(s, &v)) return "int";
  return s.size() <= 44 ? "embstr" : "raw";
}

bool Keyspace::expire_if_needed(std::string_view key) {
  const int64_t* when = expires_.find(key);
  if (when == nullptr || *when > clock_.now_ms()) return false;
  expires_.erase(key);
  entries_.erase(key);
  return true;
}

void Keyspace::modified(std::string_view key) {
  ++dirty_;
  if (on_modified_) on_modified_(key);
}

Value* Keyspace::find(std::string_view key) {
  expire_if_needed(key);
  Entry* e = entries_.find(key);
  if (e == nullptr) return nullptr;
  e->lru = lru_clock(clock_.now_ms());
  return e->value.get();
}

Value* Keyspace::find_for_write(std::string_view key) {
  Value* v = find(key);
  if (v != nullptr) modified(key);
  return v;
}

Value* Keyspace::set(std::string_view key, std::unique_ptr<Value> value, bool keep_ttl) {
  Entry& e = entries_.get_or_insert(key);
  e.value = std::move(value);
  e.lru = lru_clock(clock_.now_ms());
  if (!keep_ttl) expires_.erase(key);
  modified(key);
  return e.value.get();
}

bool Keyspace::erase(std::string_view key) {
  expire_if_needed(key);
  if (!entries_.erase(key)) return false;
  expires_.erase(key);
  modified(key);
  return true;
}

void Keyspace::erase_if_empty(std::string_view key) {
  const Entry* e = entries_.find(key);
  if (e != nullptr && e->value->empty()) {
    entries_.erase(key);
    expires_.erase(key);
  }
}

void Keyspace::clear() {
  entries_.clear();
  expires_.clear();
  ++dirty_;
  if (on_flush_) on_flush_();
}

bool Keyspace::is_expired(std::string_view key) const {
  const int64_t* when = expires_.find(key);
  return when != nullptr && *when <= clock_.now_ms();
}

bool Keyspace::set_expire(std::string_view key, int64_t when_ms) {
  if (entries_.find(key) == nullptr) return false;
  expires_.get_or_insert(key) = when_ms;
  return true;
}

std::optional<int64_t> Keyspace::get_expire(std::string_view key) const {
  const int64_t* when = expires_.find(key);
  return when ? std::optional<int64_t>(*when) : std::nullopt;
}

bool Keyspace::persist(std::string_view key) { return expires_.erase(key); }

void register_keyspace_commands(CommandTable& table, Keyspace& ks) {
  table.add({"get", 2, [&ks](CommandContext& c) {
               auto [sv, wrong] = lookup<StringValue>(ks, c.args[1], false);
               if (wrong) return wrong_type(c);
               if (sv) {
                 resp::append_bulk_string(c.reply, sv->s);
               } else {
                 resp::append_null_bulk(c.reply);
               }
             }});

  table.add({"set", -3, [&ks](CommandContext& c) {
               bool nx = false, xx = false, get = false, keepttl = false;
               int expire_kind = 0;  // 1 EX, 2 PX, 3 EXAT, 4 PXAT
               std::string_view expire_arg;
               for (size_t i = 3; i < c.args.size(); ++i) {
                 const std::string& opt = c.args[i];
                 bool has_next = i + 1 < c.args.size();
                 if (equals_ignore_case(opt, "nx") && !xx) {
                   nx = true;
                 } else if (equals_ignore_case(opt, "xx") && !nx) {
                   xx = true;
                 } else if (equals_ignore_case(opt, "get")) {
                   get = true;
                 } else if (equals_ignore_case(opt, "keepttl") && expire_kind == 0) {
                   keepttl = true;
                 } else if (has_next && !keepttl && expire_kind == 0 &&
                            (equals_ignore_case(opt, "ex") || equals_ignore_case(opt, "px") ||
                             equals_ignore_case(opt, "exat") || equals_ignore_case(opt, "pxat"))) {
                   expire_kind = equals_ignore_case(opt, "ex")     ? 1
                                 : equals_ignore_case(opt, "px")   ? 2
                                 : equals_ignore_case(opt, "exat") ? 3
                                                                   : 4;
                   expire_arg = c.args[++i];
                 } else {
                   resp::append_error(c.reply, kSyntax);
                   return;
                 }
               }
               int64_t when = 0;
               if (expire_kind != 0) {
                 int64_t n;
                 if (!resp::parse_int64(expire_arg, &n)) {
                   resp::append_error(c.reply, kNotInteger);
                   return;
                 }
                 bool seconds = expire_kind == 1 || expire_kind == 3;
                 if (n <= 0 || (seconds && n > std::numeric_limits<int64_t>::max() / 1000)) {
                   resp::append_error(c.reply, "ERR invalid expire time in 'set' command");
                   return;
                 }
                 when = seconds ? n * 1000 : n;
                 if (expire_kind <= 2) {
                   int64_t now = ks.clock().now_ms();
                   if (when > std::numeric_limits<int64_t>::max() - now) {
                     resp::append_error(c.reply, "ERR invalid expire time in 'set' command");
                     return;
                   }
                   when += now;
                 }
               }
               const std::string& key = c.args[1];
               Value* old = ks.find(key);
               if (get && old != nullptr && old->type() != Type::kString) return wrong_type(c);
               std::optional<std::string> old_value;
               if (old != nullptr && old->type() == Type::kString) {
                 old_value = static_cast<StringValue*>(old)->s;
               }
               bool skip = (nx && old != nullptr) || (xx && old == nullptr);
               if (!skip) {
                 ks.set(key, std::make_unique<StringValue>(c.args[2]), keepttl);
                 if (expire_kind != 0) {
                   ks.set_expire(key, when);
                   if (when <= ks.clock().now_ms()) ks.erase(key);
                 }
               }
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

  table.add({"del", -2, [&ks](CommandContext& c) {
               int64_t removed = 0;
               for (size_t i = 1; i < c.args.size(); ++i) removed += ks.erase(c.args[i]) ? 1 : 0;
               resp::append_integer(c.reply, removed);
             }});

  table.add({"exists", -2, [&ks](CommandContext& c) {
               int64_t n = 0;
               for (size_t i = 1; i < c.args.size(); ++i) n += ks.find(c.args[i]) ? 1 : 0;
               resp::append_integer(c.reply, n);
             }});

  table.add({"incr", 2, [&ks](CommandContext& c) { incr_by(ks, c, 1); }});
  table.add({"decr", 2, [&ks](CommandContext& c) { incr_by(ks, c, -1); }});
  table.add({"incrby", 3, [&ks](CommandContext& c) {
               int64_t by;
               if (!resp::parse_int64(c.args[2], &by)) return resp::append_error(c.reply, kNotInteger);
               incr_by(ks, c, by);
             }});
  table.add({"decrby", 3, [&ks](CommandContext& c) {
               int64_t by;
               if (!resp::parse_int64(c.args[2], &by)) return resp::append_error(c.reply, kNotInteger);
               if (by == std::numeric_limits<int64_t>::min()) {
                 return resp::append_error(c.reply, "ERR decrement would overflow");
               }
               incr_by(ks, c, -by);
             }});

  table.add({"append", 3, [&ks](CommandContext& c) {
               auto [sv, wrong] = lookup<StringValue>(ks, c.args[1], true);
               if (wrong) return wrong_type(c);
               if (sv == nullptr) {
                 sv = static_cast<StringValue*>(ks.set(c.args[1], std::make_unique<StringValue>()));
               }
               sv->s += c.args[2];
               sv->raw = true;
               resp::append_integer(c.reply, static_cast<int64_t>(sv->s.size()));
             }});

  table.add({"strlen", 2, [&ks](CommandContext& c) {
               auto [sv, wrong] = lookup<StringValue>(ks, c.args[1], false);
               if (wrong) return wrong_type(c);
               resp::append_integer(c.reply, sv ? static_cast<int64_t>(sv->s.size()) : 0);
             }});

  table.add({"mget", -2, [&ks](CommandContext& c) {
               resp::append_array_header(c.reply, c.args.size() - 1);
               for (size_t i = 1; i < c.args.size(); ++i) {
                 Value* v = ks.find(c.args[i]);
                 if (v != nullptr && v->type() == Type::kString) {
                   resp::append_bulk_string(c.reply, static_cast<StringValue*>(v)->s);
                 } else {
                   resp::append_null_bulk(c.reply);
                 }
               }
             }});

  table.add({"mset", -3, [&ks](CommandContext& c) {
               if (c.args.size() % 2 == 0) return resp::append_error(c.reply, arity_error("mset"));
               for (size_t i = 1; i < c.args.size(); i += 2) {
                 ks.set(c.args[i], std::make_unique<StringValue>(c.args[i + 1]));
               }
               resp::append_simple_string(c.reply, "OK");
             }});

  table.add({"type", 2, [&ks](CommandContext& c) {
               Value* v = ks.find(c.args[1]);
               resp::append_simple_string(c.reply, v ? type_name(v->type()) : "none");
             }});

  table.add({"object", -2, [&ks](CommandContext& c) {
               if (equals_ignore_case(c.args[1], "encoding")) {
                 if (c.args.size() != 3) return resp::append_error(c.reply, arity_error("object|encoding"));
                 Value* v = ks.find(c.args[2]);
                 if (v) {
                   resp::append_bulk_string(c.reply, v->encoding());
                 } else {
                   resp::append_null_bulk(c.reply);
                 }
                 return;
               }
               resp::append_error(c.reply, "ERR unknown subcommand '" + c.args[1] + "'. Try OBJECT HELP.");
             }});
}

}  // namespace rd
