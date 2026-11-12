#include "resp/parser/parser.h"

#include <cstring>

#include "common/check.h"

namespace rd::resp {

bool parse_int64(std::string_view s, int64_t* out) {
  if (s.empty()) return false;
  if (s == "0") {
    *out = 0;
    return true;
  }
  bool negative = s[0] == '-';
  size_t i = negative ? 1 : 0;
  if (i >= s.size() || s[i] < '1' || s[i] > '9') return false;
  const uint64_t limit = negative ? uint64_t{1} << 63 : (uint64_t{1} << 63) - 1;
  uint64_t v = 0;
  for (; i < s.size(); ++i) {
    if (s[i] < '0' || s[i] > '9') return false;
    auto d = static_cast<uint64_t>(s[i] - '0');
    if (v > (limit - d) / 10) return false;
    v = v * 10 + d;
  }
  *out = negative ? static_cast<int64_t>(0 - v) : static_cast<int64_t>(v);
  return true;
}

namespace {

ParseResult error(std::string msg) {
  ParseResult r;
  r.status = ParseStatus::kError;
  r.error = std::move(msg);
  return r;
}

// The line starting at `pos`: true and its end ('\r') in *cr if the '\r' and one more
// byte have arrived.
bool find_line(std::string_view in, size_t pos, size_t* cr) {
  size_t i = in.find('\r', pos);
  if (i == std::string_view::npos || i + 1 >= in.size()) return false;
  *cr = i;
  return true;
}

}  // namespace

ParseResult parse_request(std::string_view in) {
  if (in.empty()) return {};
  if (in[0] != '*') return parse_inline(in);

  size_t cr;
  if (!find_line(in, 0, &cr)) {
    if (in.find('\r') == std::string_view::npos && in.size() > kMaxInlineSize) {
      return error("ERR Protocol error: too big mbulk count string");
    }
    return {};
  }
  int64_t count;
  if (!parse_int64(in.substr(1, cr - 1), &count) || count > kMaxMultibulkLength) {
    return error("ERR Protocol error: invalid multibulk length");
  }
  size_t pos = cr + 2;
  ParseResult r;
  if (count <= 0) {
    r.status = ParseStatus::kOk;
    r.consumed = pos;
    return r;
  }
  for (int64_t i = 0; i < count; ++i) {
    if (!find_line(in, pos, &cr)) return {};
    if (in[pos] != '$') return error(std::string("ERR Protocol error: expected '$', got '") + in[pos] + "'");
    int64_t len;
    if (!parse_int64(in.substr(pos + 1, cr - pos - 1), &len) || len < 0 || len > kMaxBulkLength) {
      return error("ERR Protocol error: invalid bulk length");
    }
    pos = cr + 2;
    if (in.size() - pos < static_cast<size_t>(len) + 2) return {};
    r.args.emplace_back(in.substr(pos, static_cast<size_t>(len)));
    pos += static_cast<size_t>(len) + 2;
  }
  r.status = ParseStatus::kOk;
  r.consumed = pos;
  return r;
}

namespace {

bool is_hex(char c) {
  return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}

int hex_value(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  return c - 'A' + 10;
}

bool is_space(char c) {
  return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\v' || c == '\f';
}

// Redis's sdssplitargs over one line. False on unbalanced quotes.
bool split_args(std::string_view line, std::vector<std::string>* out) {
  size_t p = 0;
  auto at = [&](size_t i) { return i < line.size() ? line[i] : '\0'; };
  for (;;) {
    while (p < line.size() && is_space(line[p])) ++p;
    if (p >= line.size()) return true;
    std::string cur;
    bool in_double = false, in_single = false, done = false;
    while (!done) {
      char c = at(p);
      if (in_double) {
        if (c == '\\' && at(p + 1) == 'x' && is_hex(at(p + 2)) && is_hex(at(p + 3))) {
          cur += static_cast<char>(hex_value(at(p + 2)) * 16 + hex_value(at(p + 3)));
          p += 3;
        } else if (c == '\\' && at(p + 1) != '\0') {
          ++p;
          switch (at(p)) {
            case 'n': cur += '\n'; break;
            case 'r': cur += '\r'; break;
            case 't': cur += '\t'; break;
            case 'b': cur += '\b'; break;
            case 'a': cur += '\a'; break;
            default: cur += at(p); break;
          }
        } else if (c == '"') {
          if (at(p + 1) != '\0' && !is_space(at(p + 1))) return false;
          done = true;
        } else if (c == '\0') {
          return false;
        } else {
          cur += c;
        }
      } else if (in_single) {
        if (c == '\\' && at(p + 1) == '\'') {
          ++p;
          cur += '\'';
        } else if (c == '\'') {
          if (at(p + 1) != '\0' && !is_space(at(p + 1))) return false;
          done = true;
        } else if (c == '\0') {
          return false;
        } else {
          cur += c;
        }
      } else {
        switch (c) {
          case ' ': case '\n': case '\r': case '\t': case '\0': done = true; break;
          case '"': in_double = true; break;
          case '\'': in_single = true; break;
          default: cur += c; break;
        }
      }
      if (p < line.size()) ++p;
    }
    out->push_back(std::move(cur));
  }
}

}  // namespace

ParseResult parse_inline(std::string_view in) {
  RD_CHECK(!in.empty() && in[0] != '*');
  size_t nl = in.find('\n');
  if (nl == std::string_view::npos) {
    if (in.size() > kMaxInlineSize) return error("ERR Protocol error: too big inline request");
    return {};
  }
  size_t end = nl;
  if (end > 0 && in[end - 1] == '\r') --end;
  ParseResult r;
  if (!split_args(in.substr(0, end), &r.args)) {
    return error("ERR Protocol error: unbalanced quotes in request");
  }
  r.status = ParseStatus::kOk;
  r.consumed = nl + 1;
  return r;
}

}  // namespace rd::resp
