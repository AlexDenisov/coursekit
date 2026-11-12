#pragma once

// Test helper: replay a transcript recorded from real Redis
// (`python3 tools/redis_reference.py tools/cases/strings.txt` prints exactly these
// {{args}, reply} lines) against your commands, step by step.

#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "cmd/dispatch/dispatch.h"
#include "db/strings/strings.h"

namespace rd::testing {

struct Step {
  std::vector<std::string> args;
  std::string reply;
};

inline std::string show_args(const std::vector<std::string>& args) {
  std::string s;
  for (const auto& a : args) s += (s.empty() ? "" : " ") + ::testing::PrintToString(a);
  return s;
}

// Runs each step in order on one table and checks its reply. Stops at the first
// mismatch: later steps depend on earlier state.
inline void play(const CommandTable& table, const std::vector<Step>& steps) {
  for (size_t i = 0; i < steps.size(); ++i) {
    std::string reply;
    table.dispatch(steps[i].args, reply);
    ASSERT_EQ(reply, steps[i].reply) << "step " << i + 1 << ": " << show_args(steps[i].args);
  }
}

}  // namespace rd::testing
