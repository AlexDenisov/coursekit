#include "db/zsets/zsets.h"

#include <cerrno>
#include <charconv>
#include <cmath>
#include <cstdlib>
#include <memory>
#include <optional>
#include <vector>

#include "resp/encoder/encoder.h"
#include "resp/parser/parser.h"

namespace rd {

namespace {

constexpr char kNotFloat[] = "ERR value is not a valid float";
constexpr char kRangeNotFloat[] = "ERR min or max is not a float";

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

void wrong_type(CommandContext& c) { resp::append_error(c.reply, kWrongTypeError); }

struct Bound {
  double value;
  bool exclusive;
};

bool parse_bound(std::string_view s, Bound* b) {
  b->exclusive = !s.empty() && s[0] == '(';
  if (b->exclusive) s.remove_prefix(1);
  return parse_score(s, &b->value);
}

bool in_min(double v, Bound min) { return min.exclusive ? v > min.value : v >= min.value; }
bool in_max(double v, Bound max) { return max.exclusive ? v < max.value : v <= max.value; }

void reply_nodes(CommandContext& c, const std::vector<const SkipList::Node*>& nodes, bool withscores) {
  resp::append_array_header(c.reply, nodes.size() * (withscores ? 2 : 1));
  for (auto* n : nodes) {
    resp::append_bulk_string(c.reply, n->member);
    if (withscores) resp::append_bulk_string(c.reply, format_score(n->score));
  }
}

ZSetValue* get_or_create(Keyspace& ks, const std::string& key, ZSetValue* z) {
  return z ? z : static_cast<ZSetValue*>(ks.set(key, std::make_unique<ZSetValue>()));
}

void zadd_generic(Keyspace& ks, CommandContext& c, bool nx, bool xx, bool gt, bool lt, bool ch, bool incr,
                  size_t first) {
  size_t pairs = (c.args.size() - first) / 2;
  if ((c.args.size() - first) % 2 != 0 || pairs == 0) return resp::append_error(c.reply, "ERR syntax error");
  if (nx && xx) return resp::append_error(c.reply, "ERR XX and NX options at the same time are not compatible");
  if ((gt && nx) || (lt && nx) || (gt && lt)) {
    return resp::append_error(c.reply, "ERR GT, LT, and/or NX options at the same time are not compatible");
  }
  if (incr && pairs > 1) return resp::append_error(c.reply, "ERR INCR option supports a single increment-element pair");
  std::vector<double> scores(pairs);
  for (size_t i = 0; i < pairs; ++i) {
    if (!parse_score(c.args[first + 2 * i], &scores[i])) return resp::append_error(c.reply, kNotFloat);
  }
  auto [z, wrong] = lookup<ZSetValue>(ks, c.args[1], true);
  if (wrong) return wrong_type(c);
  int64_t added = 0, changed = 0;
  std::optional<double> incr_result;
  for (size_t i = 0; i < pairs; ++i) {
    const std::string& member = c.args[first + 2 * i + 1];
    double score = scores[i];
    const double* cur = z ? static_cast<const Dict<double>&>(z->scores).find(member) : nullptr;
    if (cur) {
      if (nx) continue;
      double target = incr ? *cur + score : score;
      if (std::isnan(target)) return resp::append_error(c.reply, "ERR resulting score is not a number (NaN)");
      if ((gt && target <= *cur) || (lt && target >= *cur)) continue;
      if (incr) incr_result = target;
      if (target != *cur) {
        z->set(member, target);
        ++changed;
      }
    } else {
      if (xx) continue;
      z = get_or_create(ks, c.args[1], z);
      z->set(member, score);
      ++added;
      if (incr) incr_result = score;
    }
  }
  if (incr) {
    if (incr_result) return resp::append_bulk_string(c.reply, format_score(*incr_result));
    return resp::append_null_bulk(c.reply);
  }
  resp::append_integer(c.reply, added + (ch ? changed : 0));
}

void range_by_score(Keyspace& ks, CommandContext& c, std::string_view lo, std::string_view hi, bool rev, bool withscores,
                    bool has_limit, int64_t offset, int64_t count) {
  Bound min, max;
  if (!parse_bound(rev ? hi : lo, &min) || !parse_bound(rev ? lo : hi, &max)) {
    return resp::append_error(c.reply, kRangeNotFloat);
  }
  auto [z, wrong] = lookup<ZSetValue>(ks, c.args[1], false);
  if (wrong) return wrong_type(c);
  std::vector<const SkipList::Node*> out;
  if (z && !(has_limit && offset < 0)) {
    const SkipList::Node* n = rev ? z->order.last_until(max.value, max.exclusive)
                                  : z->order.first_from(min.value, min.exclusive);
    for (; n && (rev ? in_min(n->score, min) : in_max(n->score, max)); n = rev ? n->backward : n->next()) {
      if (offset > 0) {
        --offset;
        continue;
      }
      if (has_limit && count >= 0 && static_cast<int64_t>(out.size()) >= count) break;
      out.push_back(n);
    }
  }
  reply_nodes(c, out, withscores);
}

}  // namespace

size_t ZSetValue::approx_bytes() const {
  size_t n = sizeof(*this);
  for (auto* x = order.first(); x; x = x->next()) n += 2 * x->member.capacity() + 64 + 16 * x->levels.size();
  return n;
}

bool ZSetValue::set(std::string_view member, double score) {
  double* cur = scores.find(member);
  if (cur) {
    if (*cur != score) order.update_score(*cur, member, score);
    *cur = score;
    return false;
  }
  scores.insert(std::string(member), score);
  order.insert(score, std::string(member));
  return true;
}

bool ZSetValue::remove(std::string_view member) {
  double* cur = scores.find(member);
  if (!cur) return false;
  order.erase(*cur, member);
  scores.erase(member);
  return true;
}

std::string format_score(double v) {
  if (std::isinf(v)) return v > 0 ? "inf" : "-inf";
  if (std::isnan(v)) return "nan";
  if (v == 0) return "0";
  if (std::nearbyint(v) == v && std::fabs(v) <= 4611686018427387904.0) {
    return std::to_string(static_cast<long long>(v));
  }
  char buf[64];
  auto res = std::to_chars(buf, buf + sizeof buf, v, std::chars_format::scientific);
  std::string_view sci(buf, static_cast<size_t>(res.ptr - buf));  // [-]d[.ddd]e[+-]XX
  bool neg = sci[0] == '-';
  if (neg) sci.remove_prefix(1);
  size_t e = sci.find('e');
  std::string digits;
  for (char ch : sci.substr(0, e)) {
    if (ch != '.') digits += ch;
  }
  int exp10 = std::atoi(std::string(sci.substr(e + 1)).c_str());  // v = d.ddd * 10^exp10
  int n = static_cast<int>(digits.size());
  int K = exp10 - (n - 1);  // v = digits * 10^K
  int ax = std::abs(K + n - 1);
  std::string out = neg ? "-" : "";
  if (K >= 0 && ax < n + 7) {
    out += digits;
    out.append(static_cast<size_t>(K), '0');
    return out;
  }
  if (K < 0 && (K > -7 || ax < 4)) {
    int offset = n + K;
    if (offset <= 0) {
      out += "0.";
      out.append(static_cast<size_t>(-offset), '0');
      out += digits;
    } else {
      out += digits.substr(0, static_cast<size_t>(offset));
      out += '.';
      out += digits.substr(static_cast<size_t>(offset));
    }
    return out;
  }
  int nd = std::min(n, 18 - (neg ? 1 : 0));
  out += digits[0];
  if (nd > 1) {
    out += '.';
    out += digits.substr(1, static_cast<size_t>(nd - 1));
  }
  out += 'e';
  out += K + n - 1 < 0 ? '-' : '+';
  if (ax < 10) out += '0';
  out += std::to_string(ax);
  return out;
}

bool parse_score(std::string_view s, double* out) {
  if (s.empty() || std::isspace(static_cast<unsigned char>(s[0]))) return false;
  std::string copy(s);
  char* end = nullptr;
  errno = 0;
  double v = std::strtod(copy.c_str(), &end);
  if (end != copy.c_str() + copy.size() || std::isnan(v)) return false;
  *out = v;
  return true;
}

void register_zset_commands(CommandTable& table, Keyspace& ks) {
  table.add({"zadd", -4, [&ks](CommandContext& c) {
               bool nx = false, xx = false, gt = false, lt = false, ch = false, incr = false;
               size_t i = 2;
               for (; i < c.args.size(); ++i) {
                 const std::string& o = c.args[i];
                 if (eq(o, "nx")) nx = true;
                 else if (eq(o, "xx")) xx = true;
                 else if (eq(o, "gt")) gt = true;
                 else if (eq(o, "lt")) lt = true;
                 else if (eq(o, "ch")) ch = true;
                 else if (eq(o, "incr")) incr = true;
                 else break;
               }
               zadd_generic(ks, c, nx, xx, gt, lt, ch, incr, i);
             }});
  table.add({"zincrby", 4, [&ks](CommandContext& c) {
               zadd_generic(ks, c, false, false, false, false, false, true, 2);
             }});
  table.add({"zscore", 3, [&ks](CommandContext& c) {
               auto [z, wrong] = lookup<ZSetValue>(ks, c.args[1], false);
               if (wrong) return wrong_type(c);
               const double* s = z ? static_cast<const Dict<double>&>(z->scores).find(c.args[2]) : nullptr;
               if (s) return resp::append_bulk_string(c.reply, format_score(*s));
               resp::append_null_bulk(c.reply);
             }});
  table.add({"zmscore", -3, [&ks](CommandContext& c) {
               auto [z, wrong] = lookup<ZSetValue>(ks, c.args[1], false);
               if (wrong) return wrong_type(c);
               resp::append_array_header(c.reply, c.args.size() - 2);
               for (size_t i = 2; i < c.args.size(); ++i) {
                 const double* s = z ? static_cast<const Dict<double>&>(z->scores).find(c.args[i]) : nullptr;
                 if (s) {
                   resp::append_bulk_string(c.reply, format_score(*s));
                 } else {
                   resp::append_null_bulk(c.reply);
                 }
               }
             }});
  table.add({"zcard", 2, [&ks](CommandContext& c) {
               auto [z, wrong] = lookup<ZSetValue>(ks, c.args[1], false);
               if (wrong) return wrong_type(c);
               resp::append_integer(c.reply, z ? static_cast<int64_t>(z->scores.size()) : 0);
             }});
  auto rank = [&ks](CommandContext& c, bool rev) {
    if (c.args.size() > 4 || (c.args.size() == 4 && !eq(c.args[3], "withscore"))) {
      return resp::append_error(c.reply, "ERR syntax error");
    }
    auto [z, wrong] = lookup<ZSetValue>(ks, c.args[1], false);
    if (wrong) return wrong_type(c);
    const double* s = z ? static_cast<const Dict<double>&>(z->scores).find(c.args[2]) : nullptr;
    if (!s) return resp::append_null_bulk(c.reply);
    int64_t r = z->order.rank(*s, c.args[2]);
    if (rev) r = static_cast<int64_t>(z->order.size()) - 1 - r;
    if (c.args.size() == 4) {
      resp::append_array_header(c.reply, 2);
      resp::append_integer(c.reply, r);
      return resp::append_bulk_string(c.reply, format_score(*s));
    }
    resp::append_integer(c.reply, r);
  };
  table.add({"zrank", -3, [rank](CommandContext& c) { rank(c, false); }});
  table.add({"zrevrank", -3, [rank](CommandContext& c) { rank(c, true); }});
  table.add({"zrem", -3, [&ks](CommandContext& c) {
               auto [z, wrong] = lookup<ZSetValue>(ks, c.args[1], true);
               if (wrong) return wrong_type(c);
               int64_t n = 0;
               if (z) {
                 for (size_t i = 2; i < c.args.size(); ++i) n += z->remove(c.args[i]) ? 1 : 0;
                 ks.erase_if_empty(c.args[1]);
               }
               resp::append_integer(c.reply, n);
             }});
  table.add({"zcount", 4, [&ks](CommandContext& c) {
               Bound min, max;
               if (!parse_bound(c.args[2], &min) || !parse_bound(c.args[3], &max)) {
                 return resp::append_error(c.reply, kRangeNotFloat);
               }
               auto [z, wrong] = lookup<ZSetValue>(ks, c.args[1], false);
               if (wrong) return wrong_type(c);
               if (!z) return resp::append_integer(c.reply, 0);
               auto* first = z->order.first_from(min.value, min.exclusive);
               auto* last = z->order.last_until(max.value, max.exclusive);
               if (!first || !last) return resp::append_integer(c.reply, 0);
               int64_t a = z->order.rank(first->score, first->member);
               int64_t b = z->order.rank(last->score, last->member);
               resp::append_integer(c.reply, b >= a ? b - a + 1 : 0);
             }});
  table.add({"zrange", -4, [&ks](CommandContext& c) {
               bool byscore = false, rev = false, withscores = false, has_limit = false;
               int64_t offset = 0, count = -1;
               for (size_t i = 4; i < c.args.size(); ++i) {
                 const std::string& o = c.args[i];
                 if (eq(o, "byscore")) byscore = true;
                 else if (eq(o, "rev")) rev = true;
                 else if (eq(o, "withscores")) withscores = true;
                 else if (eq(o, "limit") && i + 2 < c.args.size()) {
                   if (!resp::parse_int64(c.args[i + 1], &offset) || !resp::parse_int64(c.args[i + 2], &count)) {
                     return resp::append_error(c.reply, "ERR value is not an integer or out of range");
                   }
                   has_limit = true;
                   i += 2;
                 } else {
                   return resp::append_error(c.reply, "ERR syntax error");
                 }
               }
               if (has_limit && !byscore) {
                 return resp::append_error(
                     c.reply, "ERR syntax error, LIMIT is only supported in combination with either BYSCORE or BYLEX");
               }
               if (byscore) return range_by_score(ks, c, c.args[2], c.args[3], rev, withscores, has_limit, offset, count);
               int64_t start, stop;
               if (!resp::parse_int64(c.args[2], &start) || !resp::parse_int64(c.args[3], &stop)) {
                 return resp::append_error(c.reply, "ERR value is not an integer or out of range");
               }
               auto [z, wrong] = lookup<ZSetValue>(ks, c.args[1], false);
               if (wrong) return wrong_type(c);
               std::vector<const SkipList::Node*> out;
               if (z) {
                 auto len = static_cast<int64_t>(z->order.size());
                 if (start < 0) start += len;
                 if (stop < 0) stop += len;
                 if (start < 0) start = 0;
                 if (stop >= len) stop = len - 1;
                 if (start <= stop && start < len) {
                   const SkipList::Node* n = z->order.at(static_cast<size_t>(rev ? len - 1 - start : start));
                   for (int64_t i = start; i <= stop && n; ++i, n = rev ? n->backward : n->next()) out.push_back(n);
                 }
               }
               reply_nodes(c, out, withscores);
             }});
  table.add({"zrangebyscore", -4, [&ks](CommandContext& c) {
               bool withscores = false, has_limit = false;
               int64_t offset = 0, count = -1;
               for (size_t i = 4; i < c.args.size(); ++i) {
                 if (eq(c.args[i], "withscores")) {
                   withscores = true;
                 } else if (eq(c.args[i], "limit") && i + 2 < c.args.size()) {
                   if (!resp::parse_int64(c.args[i + 1], &offset) || !resp::parse_int64(c.args[i + 2], &count)) {
                     return resp::append_error(c.reply, "ERR value is not an integer or out of range");
                   }
                   has_limit = true;
                   i += 2;
                 } else {
                   return resp::append_error(c.reply, "ERR syntax error");
                 }
               }
               range_by_score(ks, c, c.args[2], c.args[3], false, withscores, has_limit, offset, count);
             }});
}

}  // namespace rd
