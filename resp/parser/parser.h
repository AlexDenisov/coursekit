#pragma once

// resp/parser: reading client requests. The spec is this file; the story is README.md.

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace rd::resp {

// Redis's limits (PROTO_INLINE_MAX_SIZE and the default proto-max-bulk-len).
inline constexpr size_t kMaxInlineSize = 64 * 1024;
inline constexpr int64_t kMaxBulkLength = 512 * 1024 * 1024;
inline constexpr int64_t kMaxMultibulkLength = 2147483647;  // INT_MAX

// Parses a whole decimal int64 the way Redis's string2ll does, and nothing more lenient:
// an optional '-', then digits with no leading zeros ("0" alone is fine; "-0", "01",
// "+1", " 1", "1 ", "" and "1.0" are not), and the value must fit in int64_t.
// Returns false (and leaves *out alone) if s isn't such a number. O(|s|).
// Used for every length in a request, and later by INCR and friends (db/strings).
bool parse_int64(std::string_view s, int64_t* out);

enum class ParseStatus {
  kOk,          // one request parsed; `consumed` bytes of input belong to it
  kIncomplete,  // input is a valid prefix of a request: wait for more bytes
  kError,       // input can never become a valid request: reply `error`, close
};

struct ParseResult {
  ParseStatus status = ParseStatus::kIncomplete;
  // kOk: how many bytes from the front of the input this request used (> 0).
  size_t consumed = 0;
  // kOk: the arguments, args[0] being the command name. Empty for requests Redis
  // reads and ignores ("*0\r\n", "*-1\r\n", a blank inline line): skip them.
  std::vector<std::string> args;
  // kError: Redis's error message, ready for append_error:
  // "ERR Protocol error: invalid bulk length".
  std::string error;
};

// Parses the first request at the front of `in`, which is a client's unprocessed input.
// Doesn't keep state between calls: on kIncomplete, call again later with the same
// bytes plus whatever arrived since.
//
// Multibulk requests start with '*' (all clients, including redis-cli, send these):
//   "*<n>\r\n" then n times "$<len>\r\n<len bytes>\r\n"
//   - <n> and <len> are read with parse_int64. A line ends at its first '\r', and the
//     byte after the '\r' is skipped unseen (Redis doesn't check it's '\n'); so are
//     the two bytes after each payload. A line is only judged once its '\r' and the
//     byte after it have arrived; until then the result is kIncomplete.
//   - <n> > kMaxMultibulkLength or not a number: "ERR Protocol error: invalid
//     multibulk length". <n> <= 0: kOk with no args.
//   - no '\r' at all in the first line and in.size() > kMaxInlineSize:
//     "ERR Protocol error: too big mbulk count string".
//   - an element line that doesn't start with '$': "ERR Protocol error: expected '$',
//     got 'X'" (X is that byte).
//   - <len> < 0, > kMaxBulkLength or not a number: "ERR Protocol error: invalid bulk
//     length".
//   - Never allocates for elements that haven't arrived: <n> and <len> come from the
//     network.
// Anything else is an inline request (optional, see parse_inline).
// O(|in|).
ParseResult parse_request(std::string_view in);

// Optional. An inline request: one line, as typed into telnet or `nc`.
//   "SET k \"hello world\"\r\n"  ->  {"SET", "k", "hello world"}
//   - The line ends at the first '\n'; a '\r' right before it is dropped.
//   - No '\n' and in.size() > kMaxInlineSize: "ERR Protocol error: too big inline
//     request".
//   - Arguments are split on spaces and tabs. "double quotes" support \n \r \t \b \a
//     \\ \" and \xHH escapes (other \c is c); 'single quotes' support only \'. A
//     closing quote must be followed by a space or the end of the line. Unterminated
//     or badly closed quotes: "ERR Protocol error: unbalanced quotes in request".
//   - A blank line is kOk with no args.
// Precondition: in is non-empty and doesn't start with '*'. O(|in|).
ParseResult parse_inline(std::string_view in);

}  // namespace rd::resp
