#include "db/sets/sets.h"

#include <algorithm>
#include <memory>
#include <vector>

#include "resp/encoder/encoder.h"
#include "resp/parser/parser.h"

namespace rd {

namespace {

void wrong_type(CommandContext& c) { resp::append_error(c.reply, kWrongTypeError); }

// The sets for args[first..]; nullptr for missing keys. False (reply written) on a
// wrong type.
bool collect(Keyspace& ks, CommandContext& c, size_t first, std::vector<SetValue*>* out) {
  for (size_t i = first; i < c.args.size(); ++i) {
    auto [s, wrong] = lookup<SetValue>(ks, c.args[i], false);
    if (wrong) {
      wrong_type(c);
      return false;
    }
    out->push_back(s);
  }
  return true;
}

enum class Op { kInter, kUnion, kDiff };

std::vector<std::string> compute(std::vector<SetValue*> sets, Op op) {
  std::vector<std::string> result;
  if (op == Op::kInter) {
    for (SetValue* s : sets) {
      if (s == nullptr) return result;
    }
    std::sort(sets.begin(), sets.end(), [](SetValue* a, SetValue* b) { return a->members.size() < b->members.size(); });
    sets[0]->members.for_each([&](const std::string& m, const bool&) {
      for (size_t i = 1; i < sets.size(); ++i) {
        if (!static_cast<const Dict<bool>&>(sets[i]->members).find(m)) return;
      }
      result.push_back(m);
    });
  } else if (op == Op::kUnion) {
    Dict<bool> seen;
    for (SetValue* s : sets) {
      if (s == nullptr) continue;
      s->members.for_each([&](const std::string& m, const bool&) {
        if (seen.insert(m, true).second) result.push_back(m);
      });
    }
  } else {
    if (sets[0] == nullptr) return result;
    sets[0]->members.for_each([&](const std::string& m, const bool&) {
      for (size_t i = 1; i < sets.size(); ++i) {
        if (sets[i] && static_cast<const Dict<bool>&>(sets[i]->members).find(m)) return;
      }
      result.push_back(m);
    });
  }
  return result;
}

void reply_members(CommandContext& c, const std::vector<std::string>& members) {
  resp::append_array_header(c.reply, members.size());
  for (const auto& m : members) resp::append_bulk_string(c.reply, m);
}

void algebra(Keyspace& ks, CommandContext& c, Op op, bool store) {
  std::vector<SetValue*> sets;
  if (!collect(ks, c, store ? 2 : 1, &sets)) return;
  std::vector<std::string> result = compute(sets, op);
  if (!store) return reply_members(c, result);
  if (result.empty()) {
    ks.erase(c.args[1]);
  } else {
    auto s = std::make_unique<SetValue>();
    for (auto& m : result) s->members.insert(m, true);
    ks.set(c.args[1], std::move(s));
  }
  resp::append_integer(c.reply, static_cast<int64_t>(result.size()));
}

}  // namespace

size_t SetValue::approx_bytes() const {
  size_t n = sizeof(*this);
  members.for_each([&](const std::string& k, const bool&) { n += 40 + k.capacity(); });
  return n;
}

void register_set_commands(CommandTable& table, Keyspace& ks) {
  table.add({"sadd", -3, [&ks](CommandContext& c) {
               auto [s, wrong] = lookup<SetValue>(ks, c.args[1], true);
               if (wrong) return wrong_type(c);
               if (s == nullptr) s = static_cast<SetValue*>(ks.set(c.args[1], std::make_unique<SetValue>()));
               int64_t added = 0;
               for (size_t i = 2; i < c.args.size(); ++i) added += s->members.insert(c.args[i], true).second ? 1 : 0;
               resp::append_integer(c.reply, added);
             }});
  table.add({"srem", -3, [&ks](CommandContext& c) {
               auto [s, wrong] = lookup<SetValue>(ks, c.args[1], true);
               if (wrong) return wrong_type(c);
               int64_t n = 0;
               if (s) {
                 for (size_t i = 2; i < c.args.size(); ++i) n += s->members.erase(c.args[i]) ? 1 : 0;
                 ks.erase_if_empty(c.args[1]);
               }
               resp::append_integer(c.reply, n);
             }});
  table.add({"scard", 2, [&ks](CommandContext& c) {
               auto [s, wrong] = lookup<SetValue>(ks, c.args[1], false);
               if (wrong) return wrong_type(c);
               resp::append_integer(c.reply, s ? static_cast<int64_t>(s->members.size()) : 0);
             }});
  table.add({"sismember", 3, [&ks](CommandContext& c) {
               auto [s, wrong] = lookup<SetValue>(ks, c.args[1], false);
               if (wrong) return wrong_type(c);
               resp::append_integer(c.reply, s && s->members.find(c.args[2]) ? 1 : 0);
             }});
  table.add({"smismember", -3, [&ks](CommandContext& c) {
               auto [s, wrong] = lookup<SetValue>(ks, c.args[1], false);
               if (wrong) return wrong_type(c);
               resp::append_array_header(c.reply, c.args.size() - 2);
               for (size_t i = 2; i < c.args.size(); ++i) resp::append_integer(c.reply, s && s->members.find(c.args[i]) ? 1 : 0);
             }});
  table.add({"smembers", 2, [&ks](CommandContext& c) {
               auto [s, wrong] = lookup<SetValue>(ks, c.args[1], false);
               if (wrong) return wrong_type(c);
               std::vector<std::string> m;
               if (s) s->members.for_each([&](const std::string& k, bool&) { m.push_back(k); });
               reply_members(c, m);
             }});
  table.add({"sinter", -2, [&ks](CommandContext& c) { algebra(ks, c, Op::kInter, false); }});
  table.add({"sunion", -2, [&ks](CommandContext& c) { algebra(ks, c, Op::kUnion, false); }});
  table.add({"sdiff", -2, [&ks](CommandContext& c) { algebra(ks, c, Op::kDiff, false); }});
  table.add({"sinterstore", -3, [&ks](CommandContext& c) { algebra(ks, c, Op::kInter, true); }});
  table.add({"sunionstore", -3, [&ks](CommandContext& c) { algebra(ks, c, Op::kUnion, true); }});
  table.add({"sdiffstore", -3, [&ks](CommandContext& c) { algebra(ks, c, Op::kDiff, true); }});
  table.add({"smove", 4, [&ks](CommandContext& c) {
               auto [src, wrong1] = lookup<SetValue>(ks, c.args[1], true);
               if (wrong1) return wrong_type(c);
               auto [dst, wrong2] = lookup<SetValue>(ks, c.args[2], true);
               if (wrong2) return wrong_type(c);
               if (src == nullptr || !src->members.erase(c.args[3])) return resp::append_integer(c.reply, 0);
               ks.erase_if_empty(c.args[1]);
               if (dst == nullptr) dst = static_cast<SetValue*>(ks.set(c.args[2], std::make_unique<SetValue>()));
               dst->members.insert(c.args[3], true);
               resp::append_integer(c.reply, 1);
             }});
}

void register_set_optional_commands(CommandTable& table, Keyspace& ks, Rng& rng) {
  table.add({"spop", -2, [&ks, &rng](CommandContext& c) {
               if (c.args.size() > 3) return resp::append_error(c.reply, arity_error("spop"));
               int64_t count = -1;
               if (c.args.size() == 3 && (!resp::parse_int64(c.args[2], &count) || count < 0)) {
                 return resp::append_error(c.reply, "ERR value is out of range, must be positive");
               }
               auto [s, wrong] = lookup<SetValue>(ks, c.args[1], true);
               if (wrong) return wrong_type(c);
               if (s == nullptr) return count < 0 ? resp::append_null_bulk(c.reply) : resp::append_array_header(c.reply, 0);
               size_t n = count < 0 ? 1 : std::min<size_t>(static_cast<size_t>(count), s->members.size());
               std::vector<std::string> popped;
               for (size_t i = 0; i < n; ++i) {
                 std::string m = *s->members.random_key(rng);
                 s->members.erase(m);
                 popped.push_back(std::move(m));
               }
               ks.erase_if_empty(c.args[1]);
               if (count < 0) return resp::append_bulk_string(c.reply, popped[0]);
               reply_members(c, popped);
             }});
  table.add({"srandmember", -2, [&ks, &rng](CommandContext& c) {
               if (c.args.size() > 3) return resp::append_error(c.reply, arity_error("srandmember"));
               bool has_count = c.args.size() == 3;
               int64_t count = 1;
               if (has_count && !resp::parse_int64(c.args[2], &count)) {
                 return resp::append_error(c.reply, "ERR value is not an integer or out of range");
               }
               auto [s, wrong] = lookup<SetValue>(ks, c.args[1], false);
               if (wrong) return wrong_type(c);
               if (s == nullptr) return has_count ? resp::append_array_header(c.reply, 0) : resp::append_null_bulk(c.reply);
               if (!has_count) return resp::append_bulk_string(c.reply, *s->members.random_key(rng));
               std::vector<std::string> out;
               if (count < 0) {
                 for (int64_t i = 0; i < -count; ++i) out.push_back(*s->members.random_key(rng));
               } else {
                 std::vector<std::string> all;
                 s->members.for_each([&](const std::string& k, bool&) { all.push_back(k); });
                 rng.shuffle(all.begin(), all.end());
                 all.resize(std::min<size_t>(all.size(), static_cast<size_t>(count)));
                 out = std::move(all);
               }
               reply_members(c, out);
             }});
}

}  // namespace rd
