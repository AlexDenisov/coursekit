#include "persist/aof/aof.h"

#include <unistd.h>

#include <fstream>
#include <limits>
#include <sstream>

#include "resp/encoder/encoder.h"
#include "resp/parser/parser.h"

namespace rd {

std::vector<std::string> log_form(std::span<const std::string> args, int64_t now_ms) {
  // TODO(you): the two rewrites; parse numbers with resp::parse_int64 (an unparsable one
  // can't reach the log: the command would have failed and written nothing).
  return {args.begin(), args.end()};
}

Aof::~Aof() {
  // TODO(you): flush and close.
}

bool Aof::open() {
  // TODO(you): fopen(path, "ab").
  return false;
}

bool Aof::reopen() {
  // TODO(you)
  return false;
}

void Aof::append(std::span<const std::string> args) {
  // TODO(you): log_form, then RESP into buffer_ (and *capture_).
}

bool Aof::flush() {
  // TODO(you): fwrite + fflush; kAlways: fsync(fileno(file_)).
  return false;
}

void Aof::cron() {
  // TODO(you): the everysec rule.
}

void dispatch_logged(const CommandTable& table, Keyspace& ks, Aof& aof, std::span<const std::string> args,
                     std::string& reply, Session* session) {
  // TODO(you): compare ks.dirty() before and after.
}

ReplayResult replay_aof(std::string_view data, const CommandTable& table) {
  // TODO(you): parse_request in a loop.
  return {};
}

ReplayResult load_aof_file(const std::string& path, const CommandTable& table) {
  // TODO(you): read; replay; ::truncate(path, valid_bytes) if truncated.
  return {};
}

}  // namespace rd
