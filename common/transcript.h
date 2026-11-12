#pragma once

// Test helper: replay a transcript recorded from real Redis
// (`python3 tools/redis_reference.py tools/cases/<x>.txt` prints exactly these
// {{args}, reply} lines) against your commands, step by step.
//
// Replies whose order Redis doesn't define (SMEMBERS, HGETALL, KEYS, ...) are compared
// as multisets: mark the step with kAnyOrder (elements) or kAnyPairOrder (field/value
// pairs, as HGETALL replies).

#include <gtest/gtest.h>

#include <algorithm>
#include <string>
#include <utility>
#include <vector>

#include "cmd/dispatch/dispatch.h"

namespace rd::testing {

enum Order { kExact, kAnyOrder, kAnyPairOrder };

struct Step {
  std::vector<std::string> args;
  std::string reply;
  Order order = kExact;
};

inline std::string show_args(const std::vector<std::string>& args) {
  std::string s;
  for (const auto& a : args) s += (s.empty() ? "" : " ") + ::testing::PrintToString(a);
  return s;
}

// Splits a flat RESP2 array of bulk strings into its elements, or returns false.
// Independent of the exercises on purpose.
inline bool split_array(const std::string& r, std::vector<std::string>* out) {
  out->clear();
  if (r.empty() || r[0] != '*') return false;
  size_t cr = r.find("\r\n");
  if (cr == std::string::npos) return false;
  long n = std::stol(r.substr(1, cr - 1));
  size_t pos = cr + 2;
  for (long i = 0; i < n; ++i) {
    if (pos >= r.size() || r[pos] != '$') return false;
    size_t e = r.find("\r\n", pos);
    long len = std::stol(r.substr(pos + 1, e - pos - 1));
    if (len < 0) {
      out->push_back("<null>");
      pos = e + 2;
      continue;
    }
    out->push_back(r.substr(e + 2, static_cast<size_t>(len)));
    pos = e + 2 + static_cast<size_t>(len) + 2;
  }
  return pos == r.size();
}

inline std::vector<std::string> canonical(const std::string& reply, Order order) {
  std::vector<std::string> items;
  if (!split_array(reply, &items)) return {reply};
  if (order == kAnyPairOrder) {
    std::vector<std::string> pairs;
    for (size_t i = 0; i + 1 < items.size(); i += 2) pairs.push_back(items[i] + '\0' + items[i + 1]);
    items = std::move(pairs);
  }
  std::sort(items.begin(), items.end());
  return items;
}

// Runs each step in order on one table and checks its reply. Stops at the first
// mismatch: later steps depend on earlier state.
inline void play(const CommandTable& table, const std::vector<Step>& steps) {
  for (size_t i = 0; i < steps.size(); ++i) {
    std::string reply;
    table.dispatch(steps[i].args, reply);
    if (steps[i].order == kExact) {
      ASSERT_EQ(reply, steps[i].reply) << "step " << i + 1 << ": " << show_args(steps[i].args);
    } else {
      ASSERT_EQ(canonical(reply, steps[i].order), canonical(steps[i].reply, steps[i].order))
          << "step " << i + 1 << " (any order): " << show_args(steps[i].args)
          << "\n  got:      " << ::testing::PrintToString(reply)
          << "\n  expected: " << ::testing::PrintToString(steps[i].reply);
    }
  }
}

}  // namespace rd::testing
