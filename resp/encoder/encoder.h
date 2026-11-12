#pragma once

// resp/encoder: writing RESP2 replies. The spec is this file; the story is README.md.
//
// Every function appends to `out` and never clears it: a server builds one reply
// buffer per client and appends reply after reply, the way Redis's addReply* family
// does. Nothing here allocates except `out` growing.

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>

namespace rd::resp {

// The longest decimal form of an int64_t: "-9223372036854775808" is 20 chars.
inline constexpr size_t kMaxInt64Chars = 20;

// Writes v in decimal into buf[0..n) and returns n: a leading '-' for negatives, no
// leading zeros, "0" for zero, no terminator. buf has room for kMaxInt64Chars.
// O(digits). Write the digits yourself (no snprintf, std::to_chars, std::to_string):
// this is Redis's ll2string, and INT64_MIN is the trap.
size_t format_int64(int64_t v, char* buf);

// "+<s>\r\n", e.g. "+OK\r\n". Precondition (checked): s contains no '\r' and no '\n'.
// O(|s|).
void append_simple_string(std::string& out, std::string_view s);

// "-<msg>\r\n". msg begins with the error code: "ERR syntax error", "WRONGTYPE ...".
// Every '\r' and '\n' inside msg is written as a space, as Redis does, so an error
// that quotes user input can't break the stream. O(|msg|).
void append_error(std::string& out, std::string_view msg);

// ":<v>\r\n", e.g. ":-3\r\n". O(1).
void append_integer(std::string& out, int64_t v);

// "$<len>\r\n<s>\r\n". Binary safe: s may hold any bytes, including '\0' and "\r\n".
// O(|s|).
void append_bulk_string(std::string& out, std::string_view s);

// "$-1\r\n": the RESP2 null (GET of a missing key). O(1).
void append_null_bulk(std::string& out);

// "*<n>\r\n": announces n elements; the caller appends them right after. O(1).
void append_array_header(std::string& out, size_t n);

// "*-1\r\n": the RESP2 null array (a BLPOP that timed out). O(1).
void append_null_array(std::string& out);

// Optional. A request the way clients send it: an array of bulk strings.
//   {"GET", "k"}  ->  "*2\r\n$3\r\nGET\r\n$1\r\nk\r\n"
// Needed by persist/aof (the log is a sequence of requests) and repl/replica (a
// primary forwards requests to its replicas). O(total size).
void append_command(std::string& out, std::span<const std::string_view> args);

}  // namespace rd::resp
