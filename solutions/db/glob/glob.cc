#include "db/glob/glob.h"

namespace rd {

namespace {

// A port of Redis's stringmatchlen_impl. at() returns '\0' past the end, standing in
// for the C string terminator the original reads.
bool match(std::string_view p, std::string_view s, bool* skip_longer, int nesting) {
  if (nesting > 1000) return false;
  auto at = [&](size_t i) { return i < p.size() ? p[i] : '\0'; };
  while (!p.empty() && !s.empty()) {
    switch (p[0]) {
      case '*': {
        while (p.size() > 1 && p[1] == '*') p.remove_prefix(1);
        if (p.size() == 1) return true;
        while (!s.empty()) {
          if (match(p.substr(1), s, skip_longer, nesting + 1)) return true;
          if (*skip_longer) return false;
          s.remove_prefix(1);
        }
        *skip_longer = true;
        return false;
      }
      case '?':
        s.remove_prefix(1);
        break;
      case '[': {
        p.remove_prefix(1);
        bool negate = at(0) == '^';
        if (negate) p.remove_prefix(1);
        bool matched = false;
        for (;;) {
          if (at(0) == '\\' && p.size() >= 2) {
            p.remove_prefix(1);
            if (p[0] == s[0]) matched = true;
          } else if (at(0) == ']') {
            break;
          } else if (p.empty()) {
            break;  // unterminated set: Redis steps back one and lets the loop advance
          } else if (p.size() >= 3 && p[1] == '-') {
            unsigned char lo = static_cast<unsigned char>(p[0]);
            unsigned char hi = static_cast<unsigned char>(p[2]);
            if (lo > hi) std::swap(lo, hi);
            auto c = static_cast<unsigned char>(s[0]);
            p.remove_prefix(2);
            if (c >= lo && c <= hi) matched = true;
          } else if (p[0] == s[0]) {
            matched = true;
          }
          p.remove_prefix(1);
        }
        if (negate) matched = !matched;
        if (!matched) return false;
        s.remove_prefix(1);
        if (p.empty()) {
          // The unterminated set consumed the whole pattern.
          if (s.empty()) return true;
          return false;
        }
        break;
      }
      case '\\':
        if (p.size() >= 2) p.remove_prefix(1);
        [[fallthrough]];
      default:
        if (p[0] != s[0]) return false;
        s.remove_prefix(1);
        break;
    }
    p.remove_prefix(1);
    if (s.empty()) {
      while (!p.empty() && p[0] == '*') p.remove_prefix(1);
      break;
    }
  }
  return p.empty() && s.empty();
}

}  // namespace

bool glob_match(std::string_view pattern, std::string_view s) {
  bool skip_longer = false;
  return match(pattern, s, &skip_longer, 0);
}

}  // namespace rd
