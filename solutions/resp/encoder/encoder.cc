#include "resp/encoder/encoder.h"

#include "common/check.h"

namespace rd::resp {

size_t format_int64(int64_t v, char* buf) {
  uint64_t mag = v < 0 ? 0 - static_cast<uint64_t>(v) : static_cast<uint64_t>(v);
  char tmp[kMaxInt64Chars];
  size_t n = 0;
  do {
    tmp[n++] = static_cast<char>('0' + mag % 10);
    mag /= 10;
  } while (mag != 0);
  size_t len = 0;
  if (v < 0) buf[len++] = '-';
  while (n > 0) buf[len++] = tmp[--n];
  return len;
}

namespace {

void append_prefixed_number(std::string& out, char prefix, int64_t v) {
  char buf[kMaxInt64Chars];
  out += prefix;
  out.append(buf, format_int64(v, buf));
  out += "\r\n";
}

}  // namespace

void append_simple_string(std::string& out, std::string_view s) {
  RD_CHECK(s.find_first_of("\r\n") == std::string_view::npos,
           "a simple string can't contain CR or LF: " << show(s));
  out += '+';
  out += s;
  out += "\r\n";
}

void append_error(std::string& out, std::string_view msg) {
  out += '-';
  size_t start = out.size();
  out += msg;
  for (size_t i = start; i < out.size(); ++i) {
    if (out[i] == '\r' || out[i] == '\n') out[i] = ' ';
  }
  out += "\r\n";
}

void append_integer(std::string& out, int64_t v) { append_prefixed_number(out, ':', v); }

void append_bulk_string(std::string& out, std::string_view s) {
  append_prefixed_number(out, '$', static_cast<int64_t>(s.size()));
  out += s;
  out += "\r\n";
}

void append_null_bulk(std::string& out) { out += "$-1\r\n"; }

void append_array_header(std::string& out, size_t n) {
  append_prefixed_number(out, '*', static_cast<int64_t>(n));
}

void append_null_array(std::string& out) { out += "*-1\r\n"; }

void append_command(std::string& out, std::span<const std::string_view> args) {
  append_array_header(out, args.size());
  for (std::string_view a : args) append_bulk_string(out, a);
}

}  // namespace rd::resp
