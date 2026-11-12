#include "msg/transactions/transactions.h"

#include "resp/encoder/encoder.h"

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

}  // namespace

Transactions::Transactions(Keyspace& ks) : ks_(ks) {
  ks_.set_on_modified([this](std::string_view key) {
    auto it = watchers_.find(key);
    if (it == watchers_.end()) return;
    for (Session* s : it->second) {
      if (auto st = states_.find(s); st != states_.end()) st->second.dirty = true;
    }
  });
  ks_.set_on_flush([this] {
    for (auto& [key, sessions] : watchers_) {
      for (Session* s : sessions) {
        if (auto st = states_.find(s); st != states_.end()) st->second.dirty = true;
      }
    }
  });
}

bool Transactions::handle(const CommandTable& table, Session& s, std::span<const std::string> args, std::string& reply) {
  const std::string& cmd = args[0];
  auto it = states_.find(&s);
  bool multi = it != states_.end() && it->second.multi;
  if (eq(cmd, "multi")) {
    if (multi) {
      resp::append_error(reply, "ERR MULTI calls can not be nested");
      return true;
    }
    states_[&s].multi = true;
    resp::append_simple_string(reply, "OK");
    return true;
  }
  if (eq(cmd, "exec")) {
    if (!multi) {
      resp::append_error(reply, "ERR EXEC without MULTI");
      return true;
    }
    unwatch_all(s);
    State st = std::move(it->second);
    states_.erase(&s);
    if (st.aborted) {
      resp::append_error(reply, "EXECABORT Transaction discarded because of previous errors.");
    } else if (st.dirty) {
      resp::append_null_array(reply);
    } else {
      resp::append_array_header(reply, st.queue.size());
      executing_ = true;
      for (const auto& q : st.queue) table.dispatch(q, reply, &s);
      executing_ = false;
    }
    return true;
  }
  if (eq(cmd, "discard")) {
    if (!multi) {
      resp::append_error(reply, "ERR DISCARD without MULTI");
      return true;
    }
    unwatch_all(s);
    states_.erase(&s);
    resp::append_simple_string(reply, "OK");
    return true;
  }
  if (eq(cmd, "watch")) {
    if (multi) {
      resp::append_error(reply, "ERR WATCH inside MULTI is not allowed");
      return true;
    }
    if (args.size() < 2) {
      resp::append_error(reply, arity_error("watch"));
      return true;
    }
    State& st = states_[&s];
    for (size_t i = 1; i < args.size(); ++i) {
      if (st.watched.insert(args[i]).second) watchers_[args[i]].insert(&s);
    }
    resp::append_simple_string(reply, "OK");
    return true;
  }
  if (eq(cmd, "unwatch")) {
    unwatch_all(s);
    if (auto i = states_.find(&s); i != states_.end()) i->second.dirty = false;
    resp::append_simple_string(reply, "OK");
    return true;
  }
  if (!multi) return false;
  std::string err = table.check(args);
  if (!err.empty()) {
    it->second.aborted = true;
    resp::append_error(reply, err);
    return true;
  }
  it->second.queue.emplace_back(args.begin(), args.end());
  resp::append_simple_string(reply, "QUEUED");
  return true;
}

bool Transactions::in_multi(const Session& s) const {
  auto it = states_.find(&s);
  return it != states_.end() && it->second.multi;
}

void Transactions::unwatch_all(Session& s) {
  auto it = states_.find(&s);
  if (it == states_.end()) return;
  for (const auto& key : it->second.watched) {
    auto w = watchers_.find(key);
    if (w == watchers_.end()) continue;
    w->second.erase(&s);
    if (w->second.empty()) watchers_.erase(w);
  }
  it->second.watched.clear();
}

void Transactions::forget(Session& s) {
  unwatch_all(s);
  states_.erase(&s);
}

}  // namespace rd
