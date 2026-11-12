#include "db/expire/expire.h"

#include <limits>
#include <optional>
#include <string>

#include "resp/encoder/encoder.h"
#include "resp/parser/parser.h"

namespace rd {

namespace {

constexpr int64_t kMax = std::numeric_limits<int64_t>::max();

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

void expire_generic(Keyspace& ks, CommandContext& c, std::string_view name, bool seconds, bool absolute) {
  int64_t n;
  if (!resp::parse_int64(c.args[2], &n)) return resp::append_error(c.reply, "ERR value is not an integer or out of range");
  bool nx = false, xx = false, gt = false, lt = false;
  for (size_t i = 3; i < c.args.size(); ++i) {
    const std::string& o = c.args[i];
    if (eq(o, "nx")) nx = true;
    else if (eq(o, "xx")) xx = true;
    else if (eq(o, "gt")) gt = true;
    else if (eq(o, "lt")) lt = true;
    else return resp::append_error(c.reply, "ERR Unsupported option " + o);
  }
  if (nx && (xx || gt || lt)) {
    return resp::append_error(c.reply, "ERR NX and XX, GT or LT options at the same time are not compatible");
  }
  if (gt && lt) return resp::append_error(c.reply, "ERR GT and LT options at the same time are not compatible");
  std::string invalid = "ERR invalid expire time in '" + std::string(name) + "' command";
  int64_t when = n;
  if (seconds) {
    if (when > kMax / 1000 || when < std::numeric_limits<int64_t>::min() / 1000) {
      return resp::append_error(c.reply, invalid);
    }
    when *= 1000;
  }
  int64_t now = ks.clock().now_ms();
  if (!absolute) {
    if (when > kMax - now) return resp::append_error(c.reply, invalid);
    when += now;
  }
  const std::string& key = c.args[1];
  if (ks.find(key) == nullptr) return resp::append_integer(c.reply, 0);
  std::optional<int64_t> cur = ks.get_expire(key);
  if ((nx && cur) || (xx && !cur) || (gt && (!cur || when <= *cur)) || (lt && cur && when >= *cur)) {
    return resp::append_integer(c.reply, 0);
  }
  if (when <= now) {
    ks.erase(key);
  } else {
    ks.set_expire(key, when);
    ks.find_for_write(key);  // counts as a write to key
  }
  resp::append_integer(c.reply, 1);
}

// kind: 0 TTL, 1 PTTL, 2 EXPIRETIME, 3 PEXPIRETIME
void ttl_generic(Keyspace& ks, CommandContext& c, int kind) {
  if (ks.find(c.args[1]) == nullptr) return resp::append_integer(c.reply, -2);
  std::optional<int64_t> when = ks.get_expire(c.args[1]);
  if (!when) return resp::append_integer(c.reply, -1);
  int64_t left = *when - ks.clock().now_ms();
  if (left < 0) left = 0;
  switch (kind) {
    case 0: return resp::append_integer(c.reply, (left + 500) / 1000);
    case 1: return resp::append_integer(c.reply, left);
    case 2: return resp::append_integer(c.reply, *when / 1000);
    default: return resp::append_integer(c.reply, *when);
  }
}

}  // namespace

void register_expire_commands(CommandTable& table, Keyspace& ks) {
  table.add({"expire", -3, [&ks](CommandContext& c) { expire_generic(ks, c, "expire", true, false); }});
  table.add({"pexpire", -3, [&ks](CommandContext& c) { expire_generic(ks, c, "pexpire", false, false); }});
  table.add({"expireat", -3, [&ks](CommandContext& c) { expire_generic(ks, c, "expireat", true, true); }});
  table.add({"pexpireat", -3, [&ks](CommandContext& c) { expire_generic(ks, c, "pexpireat", false, true); }});
  table.add({"ttl", 2, [&ks](CommandContext& c) { ttl_generic(ks, c, 0); }});
  table.add({"pttl", 2, [&ks](CommandContext& c) { ttl_generic(ks, c, 1); }});
  table.add({"expiretime", 2, [&ks](CommandContext& c) { ttl_generic(ks, c, 2); }});
  table.add({"pexpiretime", 2, [&ks](CommandContext& c) { ttl_generic(ks, c, 3); }});
  table.add({"persist", 2, [&ks](CommandContext& c) {
               bool had = ks.find(c.args[1]) != nullptr && ks.persist(c.args[1]);
               if (had) ks.find_for_write(c.args[1]);
               resp::append_integer(c.reply, had ? 1 : 0);
             }});
}

ExpireCycleStats active_expire_cycle(Keyspace& ks, Rng& rng, size_t max_loops) {
  ExpireCycleStats s;
  while (s.loops < max_loops && !ks.expires().empty()) {
    ++s.loops;
    size_t n = std::min(kExpireSamplesPerLoop, ks.expires().size());
    size_t expired = 0;
    for (size_t i = 0; i < n; ++i) {
      const std::string* k = ks.expires().random_key(rng);
      if (k == nullptr) break;
      std::string key = *k;
      ++s.sampled;
      if (ks.is_expired(key)) {
        ks.find(key);
        ++expired;
      }
    }
    s.expired += expired;
    if (expired * 100 <= n * kExpireAcceptableStalePercent) break;
  }
  return s;
}

}  // namespace rd
