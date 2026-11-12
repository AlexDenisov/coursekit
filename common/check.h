#pragma once

// Checks for programmer errors (broken contracts, violated invariants).
// A failed check prints file:line and aborts; under a debugger (lldb/gdb) it
// stops right at the failing check.
//
//   RD_CHECK(cond);
//   RD_CHECK(cond, "message, can stream values: i = " << i);
//
//   RD_CHECK_EQ(a, b);    also _NE, _LT, _LE, _GT, _GE; an optional message
//   RD_CHECK_LT(i, size(), "index out of range");          comes last
//
// The comparison forms print both sides:
//
//   core/combination/combination.cc:57: check failed: used <= capacity
//     used = 9
//     capacity = 8
//
// RD_ASSERT has the same forms as RD_CHECK but compiles to nothing with
// NDEBUG (--config=opt). Use it on hot paths you benchmark; use RD_CHECK
// everywhere else.
//
// RD_UNREACHABLE("why") marks code that must never run.
//

#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <iterator>
#include <sstream>
#include <string>
#include <string_view>
#include <type_traits>

namespace rd {

namespace detail {

template <class T>
concept Streamable = requires(std::ostream& os, const T& v) { os << v; };

template <class T>
concept Range = requires(const T& v) {
  std::begin(v);
  std::end(v);
};

}  // namespace detail

// Writes a value for a failure message: strings quoted, ranges as [a, b, c],
// types with a `void print(std::ostream&) const` member through it, anything
// else with operator<<.
template <class T>
void show_to(std::ostream& os, const T& v) {
  if constexpr (std::is_convertible_v<const T&, std::string_view>) {
    os << '"' << std::string_view(v) << '"';
  } else if constexpr (std::is_same_v<T, bool>) {
    os << (v ? "true" : "false");
  } else if constexpr (requires { v.print(os); }) {
    v.print(os);
  } else if constexpr (detail::Streamable<T>) {
    os << v;
  } else if constexpr (detail::Range<T>) {
    os << '[';
    const char* sep = "";
    for (const auto& x : v) {
      os << sep;
      show_to(os, x);
      sep = ", ";
    }
    os << ']';
  } else {
    os << "<unprintable>";
  }
}

template <class T>
std::string show(const T& v) {
  std::ostringstream os;
  show_to(os, v);
  return os.str();
}

namespace detail {

[[noreturn]] inline void check_failed(const char* file, int line, const char* what,
                                      const std::string& details) {
  std::fflush(stdout);
  std::cout.flush();
  std::fprintf(stderr, "%s:%d: %s\n%s", file, line, what, details.c_str());
  std::fflush(stderr);
  std::abort();
}

}  // namespace detail
}  // namespace rd

#define RD_CHECK(cond, ...)                                                     \
  do {                                                                              \
    if (!(cond)) [[unlikely]] {                                                     \
      std::ostringstream rd_check_os_;                                          \
      __VA_OPT__(rd_check_os_ << "  " << __VA_ARGS__ << '\n';)                  \
      ::rd::detail::check_failed(__FILE__, __LINE__, "check failed: " #cond,    \
                                     rd_check_os_.str());                       \
    }                                                                               \
  } while (0)

#define RD_CHECK_OP_(op, a, b, ...)                                                    \
  do {                                                                                     \
    auto&& rd_check_a_ = (a);                                                          \
    auto&& rd_check_b_ = (b);                                                          \
    if (!(rd_check_a_ op rd_check_b_)) [[unlikely]] {                              \
      std::ostringstream rd_check_os_;                                                 \
      rd_check_os_ << "  " #a " = " << ::rd::show(rd_check_a_) << '\n'         \
                       << "  " #b " = " << ::rd::show(rd_check_b_) << '\n';        \
      __VA_OPT__(rd_check_os_ << "  " << __VA_ARGS__ << '\n';)                         \
      ::rd::detail::check_failed(__FILE__, __LINE__, "check failed: " #a " " #op " " #b, \
                                     rd_check_os_.str());                              \
    }                                                                                      \
  } while (0)

#define RD_CHECK_EQ(a, b, ...) RD_CHECK_OP_(==, a, b __VA_OPT__(, ) __VA_ARGS__)
#define RD_CHECK_NE(a, b, ...) RD_CHECK_OP_(!=, a, b __VA_OPT__(, ) __VA_ARGS__)
#define RD_CHECK_LT(a, b, ...) RD_CHECK_OP_(<, a, b __VA_OPT__(, ) __VA_ARGS__)
#define RD_CHECK_LE(a, b, ...) RD_CHECK_OP_(<=, a, b __VA_OPT__(, ) __VA_ARGS__)
#define RD_CHECK_GT(a, b, ...) RD_CHECK_OP_(>, a, b __VA_OPT__(, ) __VA_ARGS__)
#define RD_CHECK_GE(a, b, ...) RD_CHECK_OP_(>=, a, b __VA_OPT__(, ) __VA_ARGS__)

#ifdef NDEBUG
// Not evaluated, but still compiled, so it can't rot.
#define RD_ASSERT(cond, ...)                         \
  do {                                                   \
    if (false) {                                         \
      RD_CHECK(cond __VA_OPT__(, ) __VA_ARGS__);     \
    }                                                    \
  } while (0)
#else
#define RD_ASSERT(cond, ...) RD_CHECK(cond __VA_OPT__(, ) __VA_ARGS__)
#endif

#define RD_UNREACHABLE(...)                                                       \
  do {                                                                                \
    std::ostringstream rd_check_os_;                                              \
    __VA_OPT__(rd_check_os_ << "  " << __VA_ARGS__ << '\n';)                      \
    ::rd::detail::check_failed(__FILE__, __LINE__, "unreachable code reached",    \
                                   rd_check_os_.str());                           \
  } while (0)
