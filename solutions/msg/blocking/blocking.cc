#include "msg/blocking/blocking.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>

#include "db/lists/lists.h"
#include "resp/encoder/encoder.h"

namespace rd {

namespace {

std::string pop_reply(Keyspace& ks, ListValue& list, const std::string& key, bool left) {
  std::string v = left ? std::move(list.items.front()) : std::move(list.items.back());
  if (left) {
    list.items.pop_front();
  } else {
    list.items.pop_back();
  }
  ks.erase_if_empty(key);
  std::string out;
  resp::append_array_header(out, 2);
  resp::append_bulk_string(out, key);
  resp::append_bulk_string(out, v);
  return out;
}

}  // namespace

bool Blocking::pop(Session& s, std::span<const std::string> args, bool left, bool may_block, std::string& reply) {
  std::string t = args.back();
  char* end = nullptr;
  double seconds = std::strtod(t.c_str(), &end);
  if (t.empty() || end != t.c_str() + t.size() || std::isnan(seconds) || std::isinf(seconds)) {
    resp::append_error(reply, "ERR timeout is not a float or out of range");
    return true;
  }
  if (seconds < 0) {
    resp::append_error(reply, "ERR timeout is negative");
    return true;
  }
  std::vector<std::string> keys(args.begin() + 1, args.end() - 1);
  for (const auto& key : keys) {
    auto [list, wrong] = lookup<ListValue>(ks_, key, true);
    if (wrong) {
      resp::append_error(reply, kWrongTypeError);
      return true;
    }
    if (list && !list->items.empty()) {
      reply += pop_reply(ks_, *list, key, left);
      return true;
    }
  }
  if (!may_block) {
    resp::append_null_array(reply);
    return true;
  }
  Wait w;
  w.keys = keys;
  w.left = left;
  if (seconds > 0) {
    Session* sp = &s;
    auto ms = static_cast<int64_t>(std::ceil(seconds * 1000));
    w.timer = timers_.add(clock_.now_ms() + ms, [this, sp](TimerId) {
      auto it = waiting_.find(sp);
      if (it != waiting_.end()) {
        it->second.timer = 0;
        std::string nil;
        resp::append_null_array(nil);
        sp->push(nil);
        unblock(*sp);
      }
      return kNoMore;
    });
  }
  for (const auto& key : keys) by_key_[key].push_back(&s);
  waiting_[&s] = std::move(w);
  on_block_(s);
  return false;
}

void Blocking::serve_ready() {
  bool progress = true;
  while (progress) {
    progress = false;
    for (auto it = by_key_.begin(); it != by_key_.end();) {
      std::string key = it->first;
      auto& queue = it->second;
      bool served = false;
      while (!queue.empty()) {
        auto [list, wrong] = lookup<ListValue>(ks_, key, true);
        if (wrong || list == nullptr || list->items.empty()) break;
        Session* s = queue.front();
        std::string out = pop_reply(ks_, *list, key, waiting_[s].left);
        s->push(out);
        unblock(*s);  // removes s from every queue, maybe erasing this entry
        served = progress = true;
        break;
      }
      if (served) break;  // by_key_ may have changed: start over
      ++it;
    }
  }
}

bool Blocking::blocked(const Session& s) const { return waiting_.count(const_cast<Session*>(&s)) > 0; }

void Blocking::forget(Session& s) {
  auto it = waiting_.find(&s);
  if (it == waiting_.end()) return;
  for (const auto& key : it->second.keys) {
    auto q = by_key_.find(key);
    if (q == by_key_.end()) continue;
    q->second.erase(std::remove(q->second.begin(), q->second.end(), &s), q->second.end());
    if (q->second.empty()) by_key_.erase(q);
  }
  if (it->second.timer) timers_.cancel(it->second.timer);
  waiting_.erase(it);
}

void Blocking::unblock(Session& s) {
  forget(s);
  on_unblock_(s);
}

void register_blocking_commands(CommandTable& table, Blocking& blocking, std::function<bool(Session*)> may_block) {
  struct NoSession : Session {
    void push(std::string_view) override {}
  };
  auto run = [&blocking, may_block](CommandContext& c, bool left) {
    static NoSession none;
    bool can = c.session != nullptr && may_block(c.session);
    blocking.pop(c.session ? *c.session : none, c.args, left, can, c.reply);
  };
  table.add({"blpop", -3, [run](CommandContext& c) { run(c, true); }});
  table.add({"brpop", -3, [run](CommandContext& c) { run(c, false); }});
}

}  // namespace rd
