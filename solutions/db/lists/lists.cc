#include "db/lists/lists.h"

#include <algorithm>
#include <memory>

#include "resp/encoder/encoder.h"
#include "resp/parser/parser.h"

namespace rd {

namespace {

constexpr char kNotInteger[] = "ERR value is not an integer or out of range";

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

// LRANGE's normalisation: false if the range is empty.
bool normalize(int64_t len, int64_t* start, int64_t* stop) {
  if (*start < 0) *start += len;
  if (*stop < 0) *stop += len;
  if (*start < 0) *start = 0;
  if (*start > *stop || *start >= len) return false;
  if (*stop >= len) *stop = len - 1;
  return true;
}

void push(Keyspace& ks, CommandContext& c, bool head, bool only_existing) {
  auto [list, wrong] = lookup<ListValue>(ks, c.args[1], true);
  if (wrong) return resp::append_error(c.reply, kWrongTypeError);
  if (list == nullptr) {
    if (only_existing) return resp::append_integer(c.reply, 0);
    list = static_cast<ListValue*>(ks.set(c.args[1], std::make_unique<ListValue>()));
  }
  for (size_t i = 2; i < c.args.size(); ++i) {
    if (head) {
      list->items.push_front(c.args[i]);
    } else {
      list->items.push_back(c.args[i]);
    }
  }
  resp::append_integer(c.reply, static_cast<int64_t>(list->items.size()));
}

void pop(Keyspace& ks, CommandContext& c, bool head) {
  if (c.args.size() > 3) return resp::append_error(c.reply, arity_error(head ? "lpop" : "rpop"));
  int64_t count = -1;
  if (c.args.size() == 3 && (!resp::parse_int64(c.args[2], &count) || count < 0)) {
    return resp::append_error(c.reply, "ERR value is out of range, must be positive");
  }
  auto [list, wrong] = lookup<ListValue>(ks, c.args[1], true);
  if (wrong) return resp::append_error(c.reply, kWrongTypeError);
  if (list == nullptr) {
    return count < 0 ? resp::append_null_bulk(c.reply) : resp::append_null_array(c.reply);
  }
  auto take = [&] {
    std::string v = head ? std::move(list->items.front()) : std::move(list->items.back());
    if (head) {
      list->items.pop_front();
    } else {
      list->items.pop_back();
    }
    return v;
  };
  if (count < 0) {
    resp::append_bulk_string(c.reply, take());
  } else {
    size_t n = std::min<size_t>(static_cast<size_t>(count), list->items.size());
    resp::append_array_header(c.reply, n);
    for (size_t i = 0; i < n; ++i) resp::append_bulk_string(c.reply, take());
  }
  ks.erase_if_empty(c.args[1]);
}

bool int_arg(CommandContext& c, size_t i, int64_t* out) {
  if (resp::parse_int64(c.args[i], out)) return true;
  resp::append_error(c.reply, kNotInteger);
  return false;
}

}  // namespace

void register_list_commands(CommandTable& table, Keyspace& ks) {
  table.add({"lpush", -3, [&ks](CommandContext& c) { push(ks, c, true, false); }});
  table.add({"rpush", -3, [&ks](CommandContext& c) { push(ks, c, false, false); }});
  table.add({"lpushx", -3, [&ks](CommandContext& c) { push(ks, c, true, true); }});
  table.add({"rpushx", -3, [&ks](CommandContext& c) { push(ks, c, false, true); }});
  table.add({"lpop", -2, [&ks](CommandContext& c) { pop(ks, c, true); }});
  table.add({"rpop", -2, [&ks](CommandContext& c) { pop(ks, c, false); }});

  table.add({"llen", 2, [&ks](CommandContext& c) {
               auto [list, wrong] = lookup<ListValue>(ks, c.args[1], false);
               if (wrong) return resp::append_error(c.reply, kWrongTypeError);
               resp::append_integer(c.reply, list ? static_cast<int64_t>(list->items.size()) : 0);
             }});

  table.add({"lrange", 4, [&ks](CommandContext& c) {
               int64_t start, stop;
               if (!int_arg(c, 2, &start) || !int_arg(c, 3, &stop)) return;
               auto [list, wrong] = lookup<ListValue>(ks, c.args[1], false);
               if (wrong) return resp::append_error(c.reply, kWrongTypeError);
               if (list == nullptr || !normalize(static_cast<int64_t>(list->items.size()), &start, &stop)) {
                 return resp::append_array_header(c.reply, 0);
               }
               resp::append_array_header(c.reply, static_cast<size_t>(stop - start + 1));
               for (int64_t i = start; i <= stop; ++i) {
                 resp::append_bulk_string(c.reply, list->items[static_cast<size_t>(i)]);
               }
             }});

  table.add({"lindex", 3, [&ks](CommandContext& c) {
               int64_t i;
               if (!int_arg(c, 2, &i)) return;
               auto [list, wrong] = lookup<ListValue>(ks, c.args[1], false);
               if (wrong) return resp::append_error(c.reply, kWrongTypeError);
               if (list == nullptr) return resp::append_null_bulk(c.reply);
               auto len = static_cast<int64_t>(list->items.size());
               if (i < 0) i += len;
               if (i < 0 || i >= len) return resp::append_null_bulk(c.reply);
               resp::append_bulk_string(c.reply, list->items[static_cast<size_t>(i)]);
             }});

  table.add({"lset", 4, [&ks](CommandContext& c) {
               int64_t i;
               if (!int_arg(c, 2, &i)) return;
               auto [list, wrong] = lookup<ListValue>(ks, c.args[1], true);
               if (wrong) return resp::append_error(c.reply, kWrongTypeError);
               if (list == nullptr) return resp::append_error(c.reply, "ERR no such key");
               auto len = static_cast<int64_t>(list->items.size());
               if (i < 0) i += len;
               if (i < 0 || i >= len) return resp::append_error(c.reply, "ERR index out of range");
               list->items[static_cast<size_t>(i)] = c.args[3];
               resp::append_simple_string(c.reply, "OK");
             }});

  table.add({"ltrim", 4, [&ks](CommandContext& c) {
               int64_t start, stop;
               if (!int_arg(c, 2, &start) || !int_arg(c, 3, &stop)) return;
               auto [list, wrong] = lookup<ListValue>(ks, c.args[1], true);
               if (wrong) return resp::append_error(c.reply, kWrongTypeError);
               if (list != nullptr) {
                 auto len = static_cast<int64_t>(list->items.size());
                 if (!normalize(len, &start, &stop)) {
                   list->items.clear();
                 } else {
                   list->items.erase(list->items.begin() + stop + 1, list->items.end());
                   list->items.erase(list->items.begin(), list->items.begin() + start);
                 }
                 ks.erase_if_empty(c.args[1]);
               }
               resp::append_simple_string(c.reply, "OK");
             }});
}

void register_list_optional_commands(CommandTable& table, Keyspace& ks) {
  table.add({"lrem", 4, [&ks](CommandContext& c) {
               int64_t count;
               if (!int_arg(c, 2, &count)) return;
               auto [list, wrong] = lookup<ListValue>(ks, c.args[1], true);
               if (wrong) return resp::append_error(c.reply, kWrongTypeError);
               if (list == nullptr) return resp::append_integer(c.reply, 0);
               int64_t removed = 0;
               auto& items = list->items;
               if (count >= 0) {
                 for (auto it = items.begin(); it != items.end() && (count == 0 || removed < count);) {
                   if (*it == c.args[3]) {
                     it = items.erase(it);
                     ++removed;
                   } else {
                     ++it;
                   }
                 }
               } else {
                 for (size_t i = items.size(); i-- > 0 && removed < -count;) {
                   if (items[i] == c.args[3]) {
                     items.erase(items.begin() + static_cast<long>(i));
                     ++removed;
                   }
                 }
               }
               ks.erase_if_empty(c.args[1]);
               resp::append_integer(c.reply, removed);
             }});

  table.add({"linsert", 5, [&ks](CommandContext& c) {
               bool before = eq(c.args[2], "before");
               if (!before && !eq(c.args[2], "after")) return resp::append_error(c.reply, "ERR syntax error");
               auto [list, wrong] = lookup<ListValue>(ks, c.args[1], true);
               if (wrong) return resp::append_error(c.reply, kWrongTypeError);
               if (list == nullptr) return resp::append_integer(c.reply, 0);
               auto it = std::find(list->items.begin(), list->items.end(), c.args[3]);
               if (it == list->items.end()) return resp::append_integer(c.reply, -1);
               list->items.insert(before ? it : it + 1, c.args[4]);
               resp::append_integer(c.reply, static_cast<int64_t>(list->items.size()));
             }});
}

}  // namespace rd
