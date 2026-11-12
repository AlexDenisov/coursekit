#include "db/hashes/hashes.h"

#include <limits>
#include <memory>

#include "resp/encoder/encoder.h"
#include "resp/parser/parser.h"

namespace rd {

namespace {

constexpr char kNotInteger[] = "ERR value is not an integer or out of range";

void wrong_type(CommandContext& c) { resp::append_error(c.reply, kWrongTypeError); }

}  // namespace

size_t HashValue::approx_bytes() const {
  size_t n = sizeof(*this);
  fields.for_each([&](const std::string& k, const std::string& v) { n += 48 + k.capacity() + v.capacity(); });
  return n;
}

void register_hash_commands(CommandTable& table, Keyspace& ks) {
  table.add({"hset", -4, [&ks](CommandContext& c) {
               if (c.args.size() % 2 != 0) return resp::append_error(c.reply, arity_error("hset"));
               auto [h, wrong] = lookup<HashValue>(ks, c.args[1], true);
               if (wrong) return wrong_type(c);
               if (h == nullptr) h = static_cast<HashValue*>(ks.set(c.args[1], std::make_unique<HashValue>()));
               int64_t added = 0;
               for (size_t i = 2; i < c.args.size(); i += 2) {
                 auto [v, is_new] = h->fields.insert(c.args[i], c.args[i + 1]);
                 if (is_new) {
                   ++added;
                 } else {
                   *v = c.args[i + 1];
                 }
               }
               resp::append_integer(c.reply, added);
             }});

  table.add({"hsetnx", 4, [&ks](CommandContext& c) {
               auto [h, wrong] = lookup<HashValue>(ks, c.args[1], true);
               if (wrong) return wrong_type(c);
               if (h == nullptr) h = static_cast<HashValue*>(ks.set(c.args[1], std::make_unique<HashValue>()));
               resp::append_integer(c.reply, h->fields.insert(c.args[2], c.args[3]).second ? 1 : 0);
             }});

  table.add({"hget", 3, [&ks](CommandContext& c) {
               auto [h, wrong] = lookup<HashValue>(ks, c.args[1], false);
               if (wrong) return wrong_type(c);
               const std::string* v = h ? h->fields.find(c.args[2]) : nullptr;
               if (v) {
                 resp::append_bulk_string(c.reply, *v);
               } else {
                 resp::append_null_bulk(c.reply);
               }
             }});

  table.add({"hmget", -3, [&ks](CommandContext& c) {
               auto [h, wrong] = lookup<HashValue>(ks, c.args[1], false);
               if (wrong) return wrong_type(c);
               resp::append_array_header(c.reply, c.args.size() - 2);
               for (size_t i = 2; i < c.args.size(); ++i) {
                 const std::string* v = h ? h->fields.find(c.args[i]) : nullptr;
                 if (v) {
                   resp::append_bulk_string(c.reply, *v);
                 } else {
                   resp::append_null_bulk(c.reply);
                 }
               }
             }});

  table.add({"hdel", -3, [&ks](CommandContext& c) {
               auto [h, wrong] = lookup<HashValue>(ks, c.args[1], true);
               if (wrong) return wrong_type(c);
               int64_t n = 0;
               if (h) {
                 for (size_t i = 2; i < c.args.size(); ++i) n += h->fields.erase(c.args[i]) ? 1 : 0;
                 ks.erase_if_empty(c.args[1]);
               }
               resp::append_integer(c.reply, n);
             }});

  table.add({"hlen", 2, [&ks](CommandContext& c) {
               auto [h, wrong] = lookup<HashValue>(ks, c.args[1], false);
               if (wrong) return wrong_type(c);
               resp::append_integer(c.reply, h ? static_cast<int64_t>(h->fields.size()) : 0);
             }});

  table.add({"hexists", 3, [&ks](CommandContext& c) {
               auto [h, wrong] = lookup<HashValue>(ks, c.args[1], false);
               if (wrong) return wrong_type(c);
               resp::append_integer(c.reply, h && h->fields.find(c.args[2]) ? 1 : 0);
             }});

  table.add({"hstrlen", 3, [&ks](CommandContext& c) {
               auto [h, wrong] = lookup<HashValue>(ks, c.args[1], false);
               if (wrong) return wrong_type(c);
               const std::string* v = h ? h->fields.find(c.args[2]) : nullptr;
               resp::append_integer(c.reply, v ? static_cast<int64_t>(v->size()) : 0);
             }});

  auto dump = [&ks](CommandContext& c, bool keys, bool values) {
    auto [h, wrong] = lookup<HashValue>(ks, c.args[1], false);
    if (wrong) return wrong_type(c);
    if (h == nullptr) return resp::append_array_header(c.reply, 0);
    resp::append_array_header(c.reply, h->fields.size() * ((keys ? 1 : 0) + (values ? 1 : 0)));
    h->fields.for_each([&](const std::string& k, std::string& v) {
      if (keys) resp::append_bulk_string(c.reply, k);
      if (values) resp::append_bulk_string(c.reply, v);
    });
  };
  table.add({"hgetall", 2, [dump](CommandContext& c) { dump(c, true, true); }});
  table.add({"hkeys", 2, [dump](CommandContext& c) { dump(c, true, false); }});
  table.add({"hvals", 2, [dump](CommandContext& c) { dump(c, false, true); }});

  table.add({"hincrby", 4, [&ks](CommandContext& c) {
               int64_t by;
               if (!resp::parse_int64(c.args[3], &by)) return resp::append_error(c.reply, kNotInteger);
               auto [h, wrong] = lookup<HashValue>(ks, c.args[1], true);
               if (wrong) return wrong_type(c);
               if (h == nullptr) h = static_cast<HashValue*>(ks.set(c.args[1], std::make_unique<HashValue>()));
               int64_t old = 0;
               std::string* v = h->fields.find(c.args[2]);
               if (v && !resp::parse_int64(*v, &old)) {
                 return resp::append_error(c.reply, "ERR hash value is not an integer");
               }
               if ((by > 0 && old > std::numeric_limits<int64_t>::max() - by) ||
                   (by < 0 && old < std::numeric_limits<int64_t>::min() - by)) {
                 return resp::append_error(c.reply, "ERR increment or decrement would overflow");
               }
               h->fields.get_or_insert(c.args[2]) = std::to_string(old + by);
               resp::append_integer(c.reply, old + by);
             }});
}

}  // namespace rd
