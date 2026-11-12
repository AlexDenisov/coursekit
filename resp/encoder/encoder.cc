#include "resp/encoder/encoder.h"

#include "common/check.h"

namespace rd::resp {

size_t format_int64(int64_t v, char* buf) {
  // TODO(you): write digits from the least significant end, then reverse. -v overflows
  // for INT64_MIN: work in uint64_t.
  return 0;
}

void append_simple_string(std::string& out, std::string_view s) {
  RD_CHECK(s.find_first_of("\r\n") == std::string_view::npos,
                "a simple string can't contain CR or LF: " << show(s));
  // TODO(you): '+', the text, "\r\n".
}

void append_error(std::string& out, std::string_view msg) {
  // TODO(you): '-', msg with CR and LF replaced by spaces, "\r\n".
}

void append_integer(std::string& out, int64_t v) {
  // TODO(you): ':', format_int64, "\r\n".
}

void append_bulk_string(std::string& out, std::string_view s) {
  // TODO(you): the length line, then the bytes as they are, then "\r\n".
}

void append_null_bulk(std::string& out) {
  // TODO(you): one constant.
}

void append_array_header(std::string& out, size_t n) {
  // TODO(you): like an integer, with '*'.
}

void append_null_array(std::string& out) {
  // TODO(you): one constant.
}

void append_command(std::string& out, std::span<const std::string_view> args) {
  // TODO(you, optional): an array header, then one bulk string per argument.
}

}  // namespace rd::resp
