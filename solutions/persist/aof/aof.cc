#include "persist/aof/aof.h"

#include <unistd.h>

#include <fstream>
#include <limits>
#include <sstream>

#include "resp/encoder/encoder.h"
#include "resp/parser/parser.h"

namespace rd {

namespace {

bool eq(std::string_view a, std::string_view b) {
  if (a.size() != b.size()) return false;
  for (size_t i = 0; i < a.size(); ++i) {
    char x = a[i], y = b[i];
    if (x >= 'A' && x <= 'Z') x = static_cast<char>(x - 'A' + 'a');
    if (y >= 'A' && y <= 'Z') y = static_cast<char>(y - 'A' + 'a');
    if (x != y) return false;
  }
  return true;
}

}  // namespace

std::vector<std::string> log_form(std::span<const std::string> args, int64_t now_ms) {
  std::vector<std::string> out(args.begin(), args.end());
  if (out.empty()) return out;
  int64_t n;
  if ((eq(out[0], "expire") || eq(out[0], "pexpire") || eq(out[0], "expireat")) && out.size() >= 3 &&
      resp::parse_int64(out[2], &n)) {
    int64_t ms = eq(out[0], "pexpire") ? n : n * 1000;
    out[2] = std::to_string(eq(out[0], "expireat") ? ms : now_ms + ms);
    out[0] = "PEXPIREAT";
    return out;
  }
  if (eq(out[0], "set")) {
    for (size_t i = 3; i + 1 < out.size(); ++i) {
      bool ex = eq(out[i], "ex"), px = eq(out[i], "px"), exat = eq(out[i], "exat");
      if ((ex || px || exat) && resp::parse_int64(out[i + 1], &n)) {
        out[i] = "PXAT";
        out[i + 1] = std::to_string(exat ? n * 1000 : now_ms + (ex ? n * 1000 : n));
        break;
      }
    }
  }
  return out;
}

Aof::~Aof() {
  if (file_) {
    flush();
    std::fclose(file_);
  }
}

bool Aof::open() {
  file_ = std::fopen(path_.c_str(), "ab");
  last_fsync_ms_ = clock_.now_ms();
  return file_ != nullptr;
}

bool Aof::reopen() {
  if (file_) {
    flush();
    std::fclose(file_);
    file_ = nullptr;
  }
  return open();
}

void Aof::append(std::span<const std::string> args) {
  std::vector<std::string> logged = log_form(args, clock_.now_ms());
  std::string req;
  resp::append_array_header(req, logged.size());
  for (const auto& a : logged) resp::append_bulk_string(req, a);
  buffer_ += req;
  if (capture_) *capture_ += req;
}

bool Aof::flush() {
  if (file_ == nullptr) return false;
  if (buffer_.empty()) return true;
  bool ok = std::fwrite(buffer_.data(), 1, buffer_.size(), file_) == buffer_.size() && std::fflush(file_) == 0;
  buffer_.clear();
  unsynced_ = true;
  if (ok && policy_ == FsyncPolicy::kAlways) {
    ::fsync(fileno(file_));
    ++fsyncs_;
    unsynced_ = false;
    last_fsync_ms_ = clock_.now_ms();
  }
  return ok;
}

void Aof::cron() {
  if (policy_ != FsyncPolicy::kEverySec || !unsynced_ || file_ == nullptr) return;
  if (clock_.now_ms() - last_fsync_ms_ < 1000) return;
  ::fsync(fileno(file_));
  ++fsyncs_;
  unsynced_ = false;
  last_fsync_ms_ = clock_.now_ms();
}

void dispatch_logged(const CommandTable& table, Keyspace& ks, Aof& aof, std::span<const std::string> args,
                     std::string& reply, Session* session) {
  uint64_t before = ks.dirty();
  table.dispatch(args, reply, session);
  if (ks.dirty() != before) aof.append(args);
}

ReplayResult replay_aof(std::string_view data, const CommandTable& table) {
  ReplayResult r;
  size_t pos = 0;
  while (pos < data.size()) {
    resp::ParseResult p = resp::parse_request(data.substr(pos));
    if (p.status == resp::ParseStatus::kIncomplete) {
      r.truncated = true;
      break;
    }
    if (p.status == resp::ParseStatus::kError) {
      r.error = p.error + " at byte " + std::to_string(pos);
      return r;
    }
    pos += p.consumed;
    if (p.args.empty()) continue;
    std::string reply;
    table.dispatch(p.args, reply);
    ++r.commands;
  }
  r.valid_bytes = pos;
  r.ok = true;
  return r;
}

ReplayResult load_aof_file(const std::string& path, const CommandTable& table) {
  std::ifstream f(path, std::ios::binary);
  if (!f) {
    ReplayResult r;
    r.ok = true;
    return r;
  }
  std::stringstream ss;
  ss << f.rdbuf();
  std::string data = ss.str();
  ReplayResult r = replay_aof(data, table);
  if (r.ok && r.truncated) ::truncate(path.c_str(), static_cast<off_t>(r.valid_bytes));
  return r;
}

}  // namespace rd
