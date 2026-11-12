#include "msg/pubsub/pubsub.h"

#include "db/glob/glob.h"
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

void confirm(std::string& reply, std::string_view kind, const std::string* name, size_t n) {
  resp::append_array_header(reply, 3);
  resp::append_bulk_string(reply, kind);
  if (name) {
    resp::append_bulk_string(reply, *name);
  } else {
    resp::append_null_bulk(reply);
  }
  resp::append_integer(reply, static_cast<int64_t>(n));
}

using Index = std::map<std::string, std::set<Session*>, std::less<>>;

void add(Index& index, std::set<std::string>& mine, Session& s, const std::string& name) {
  if (mine.insert(name).second) index[name].insert(&s);
}

void remove(Index& index, std::set<std::string>& mine, Session& s, const std::string& name) {
  if (mine.erase(name) == 0) return;
  auto it = index.find(name);
  if (it == index.end()) return;
  it->second.erase(&s);
  if (it->second.empty()) index.erase(it);
}

}  // namespace

void PubSub::subscribe(Session& s, std::span<const std::string> channels, std::string& reply) {
  auto& mine = by_session_[&s];
  for (const auto& ch : channels) {
    add(channels_, mine.first, s, ch);
    confirm(reply, "subscribe", &ch, mine.first.size() + mine.second.size());
  }
}

void PubSub::psubscribe(Session& s, std::span<const std::string> patterns, std::string& reply) {
  auto& mine = by_session_[&s];
  for (const auto& p : patterns) {
    add(patterns_, mine.second, s, p);
    confirm(reply, "psubscribe", &p, mine.first.size() + mine.second.size());
  }
}

void PubSub::unsubscribe(Session& s, std::span<const std::string> channels, std::string& reply) {
  auto& mine = by_session_[&s];
  std::vector<std::string> todo(channels.begin(), channels.end());
  if (todo.empty()) todo.assign(mine.first.begin(), mine.first.end());
  if (todo.empty()) confirm(reply, "unsubscribe", nullptr, mine.first.size() + mine.second.size());
  for (const auto& ch : todo) {
    remove(channels_, mine.first, s, ch);
    confirm(reply, "unsubscribe", &ch, mine.first.size() + mine.second.size());
  }
  if (mine.first.empty() && mine.second.empty()) by_session_.erase(&s);
}

void PubSub::punsubscribe(Session& s, std::span<const std::string> patterns, std::string& reply) {
  auto& mine = by_session_[&s];
  std::vector<std::string> todo(patterns.begin(), patterns.end());
  if (todo.empty()) todo.assign(mine.second.begin(), mine.second.end());
  if (todo.empty()) confirm(reply, "punsubscribe", nullptr, mine.first.size() + mine.second.size());
  for (const auto& p : todo) {
    remove(patterns_, mine.second, s, p);
    confirm(reply, "punsubscribe", &p, mine.first.size() + mine.second.size());
  }
  if (mine.first.empty() && mine.second.empty()) by_session_.erase(&s);
}

size_t PubSub::publish(std::string_view channel, std::string_view message) {
  size_t n = 0;
  if (auto it = channels_.find(channel); it != channels_.end()) {
    std::string msg;
    resp::append_array_header(msg, 3);
    resp::append_bulk_string(msg, "message");
    resp::append_bulk_string(msg, channel);
    resp::append_bulk_string(msg, message);
    for (Session* s : it->second) {
      s->push(msg);
      ++n;
    }
  }
  for (const auto& [pattern, subs] : patterns_) {
    if (!glob_match(pattern, channel)) continue;
    std::string msg;
    resp::append_array_header(msg, 4);
    resp::append_bulk_string(msg, "pmessage");
    resp::append_bulk_string(msg, pattern);
    resp::append_bulk_string(msg, channel);
    resp::append_bulk_string(msg, message);
    for (Session* s : subs) {
      s->push(msg);
      ++n;
    }
  }
  return n;
}

size_t PubSub::subscriptions(const Session& s) const {
  auto it = by_session_.find(&s);
  return it == by_session_.end() ? 0 : it->second.first.size() + it->second.second.size();
}

void PubSub::forget(Session& s) {
  auto it = by_session_.find(&s);
  if (it == by_session_.end()) return;
  auto [chs, pats] = it->second;
  for (const auto& ch : chs) remove(channels_, it->second.first, s, ch);
  for (const auto& p : pats) remove(patterns_, it->second.second, s, p);
  by_session_.erase(&s);
}

std::set<std::string> PubSub::channels(std::string_view pattern) const {
  std::set<std::string> out;
  for (const auto& [ch, subs] : channels_) {
    if (pattern == "*" || glob_match(pattern, ch)) out.insert(ch);
  }
  return out;
}

size_t PubSub::numsub(std::string_view channel) const {
  auto it = channels_.find(channel);
  return it == channels_.end() ? 0 : it->second.size();
}

size_t PubSub::numpat() const {
  size_t n = 0;
  for (const auto& [p, subs] : patterns_) n += subs.size();
  return n;
}

void register_pubsub_commands(CommandTable& table, PubSub& pubsub) {
  auto need_session = [](CommandContext& c) {
    if (c.session) return true;
    resp::append_error(c.reply, "ERR this command needs a connection");
    return false;
  };
  table.add({"subscribe", -2, [&, need_session](CommandContext& c) {
               if (need_session(c)) pubsub.subscribe(*c.session, c.args.subspan(1), c.reply);
             }});
  table.add({"psubscribe", -2, [&, need_session](CommandContext& c) {
               if (need_session(c)) pubsub.psubscribe(*c.session, c.args.subspan(1), c.reply);
             }});
  table.add({"unsubscribe", -1, [&, need_session](CommandContext& c) {
               if (need_session(c)) pubsub.unsubscribe(*c.session, c.args.subspan(1), c.reply);
             }});
  table.add({"punsubscribe", -1, [&, need_session](CommandContext& c) {
               if (need_session(c)) pubsub.punsubscribe(*c.session, c.args.subspan(1), c.reply);
             }});
  table.add({"publish", 3, [&](CommandContext& c) {
               resp::append_integer(c.reply, static_cast<int64_t>(pubsub.publish(c.args[1], c.args[2])));
             }});
  table.add({"pubsub", -2, [&](CommandContext& c) {
               if (eq(c.args[1], "channels") && c.args.size() <= 3) {
                 auto chs = pubsub.channels(c.args.size() == 3 ? std::string_view(c.args[2]) : "*");
                 resp::append_array_header(c.reply, chs.size());
                 for (auto& ch : chs) resp::append_bulk_string(c.reply, ch);
               } else if (eq(c.args[1], "numsub")) {
                 resp::append_array_header(c.reply, 2 * (c.args.size() - 2));
                 for (size_t i = 2; i < c.args.size(); ++i) {
                   resp::append_bulk_string(c.reply, c.args[i]);
                   resp::append_integer(c.reply, static_cast<int64_t>(pubsub.numsub(c.args[i])));
                 }
               } else if (eq(c.args[1], "numpat") && c.args.size() == 2) {
                 resp::append_integer(c.reply, static_cast<int64_t>(pubsub.numpat()));
               } else {
                 resp::append_error(c.reply, "ERR unknown subcommand '" + c.args[1] + "'. Try PUBSUB HELP.");
               }
             }});
}

bool pubsub_gate(const PubSub& pubsub, Session& s, std::span<const std::string> args, std::string& reply) {
  if (pubsub.subscriptions(s) == 0) return false;
  const std::string& cmd = args[0];
  for (auto ok : {"subscribe", "psubscribe", "unsubscribe", "punsubscribe", "quit", "reset", "ssubscribe", "sunsubscribe"}) {
    if (eq(cmd, ok)) return false;
  }
  if (eq(cmd, "ping")) {
    resp::append_array_header(reply, 2);
    resp::append_bulk_string(reply, "pong");
    resp::append_bulk_string(reply, args.size() > 1 ? std::string_view(args[1]) : "");
    return true;
  }
  std::string lower = cmd;
  for (char& ch : lower) {
    if (ch >= 'A' && ch <= 'Z') ch = static_cast<char>(ch - 'A' + 'a');
  }
  resp::append_error(reply, "ERR Can't execute '" + lower +
                                "': only (P|S)SUBSCRIBE / (P|S)UNSUBSCRIBE / PING / QUIT / RESET are allowed in this context");
  return true;
}

}  // namespace rd
