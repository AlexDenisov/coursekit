#include "persist/aof_rewrite/aof_rewrite.h"

#include <sys/wait.h>
#include <unistd.h>

#include <cstdio>
#include <vector>

#include "db/hashes/hashes.h"
#include "db/lists/lists.h"
#include "db/sets/sets.h"
#include "db/zsets/zsets.h"
#include "resp/encoder/encoder.h"

namespace rd {

namespace {

void emit(std::string& out, const std::vector<std::string_view>& args) {
  resp::append_array_header(out, args.size());
  for (auto a : args) resp::append_bulk_string(out, a);
}

// Emits `command key <items...>` in batches of kRewriteItemsPerCommand items (an item may
// be several arguments: `per_item`).
void emit_batched(std::string& out, std::string_view command, std::string_view key,
                  const std::vector<std::string>& args, size_t per_item) {
  size_t step = kRewriteItemsPerCommand * per_item;
  for (size_t i = 0; i < args.size(); i += step) {
    std::vector<std::string_view> cmd = {command, key};
    for (size_t j = i; j < std::min(args.size(), i + step); ++j) cmd.push_back(args[j]);
    emit(out, cmd);
  }
}

bool write_all_file(const std::string& path, std::string_view data, const char* mode) {
  FILE* f = std::fopen(path.c_str(), mode);
  if (!f) return false;
  bool ok = std::fwrite(data.data(), 1, data.size(), f) == data.size() && std::fflush(f) == 0 && ::fsync(fileno(f)) == 0;
  return std::fclose(f) == 0 && ok;
}

}  // namespace

std::string rewrite_commands(const Keyspace& ks) {
  std::string out;
  ks.entries().for_each([&](const std::string& key, const Keyspace::Entry& e) {
    if (ks.is_expired(key)) return;
    const Value& v = *e.value;
    std::vector<std::string> args;
    switch (v.type()) {
      case Type::kString:
        emit(out, {"SET", key, static_cast<const StringValue&>(v).s});
        break;
      case Type::kList:
        for (const auto& s : static_cast<const ListValue&>(v).items) args.push_back(s);
        emit_batched(out, "RPUSH", key, args, 1);
        break;
      case Type::kSet:
        static_cast<const SetValue&>(v).members.for_each([&](const std::string& m, const bool&) { args.push_back(m); });
        emit_batched(out, "SADD", key, args, 1);
        break;
      case Type::kHash:
        static_cast<const HashValue&>(v).fields.for_each([&](const std::string& f, const std::string& val) {
          args.push_back(f);
          args.push_back(val);
        });
        emit_batched(out, "HSET", key, args, 2);
        break;
      case Type::kZSet:
        for (auto* n = static_cast<const ZSetValue&>(v).order.first(); n; n = n->next()) {
          args.push_back(format_score(n->score));
          args.push_back(n->member);
        }
        emit_batched(out, "ZADD", key, args, 2);
        break;
    }
    if (auto when = ks.get_expire(key)) emit(out, {"PEXPIREAT", key, std::to_string(*when)});
  });
  return out;
}

bool AofRewriter::start() {
  if (in_progress()) return false;
  aof_.flush();
  pid_t pid = ::fork();
  if (pid < 0) return false;
  if (pid == 0) {
    bool ok = write_all_file(aof_.path() + ".rewrite", rewrite_commands(ks_), "wb");
    ::_exit(ok ? 0 : 1);
  }
  child_ = pid;
  captured_.clear();
  aof_.set_capture(&captured_);
  loop_.timers().add(ks_.clock().now_ms() + 10, [this](TimerId) -> int64_t {
    poll();
    return in_progress() ? 10 : kNoMore;
  });
  return true;
}

bool AofRewriter::poll() {
  if (!in_progress()) return false;
  int status = 0;
  pid_t r = ::waitpid(child_, &status, WNOHANG);
  if (r == 0) return false;
  child_ = -1;
  aof_.set_capture(nullptr);
  bool ok = r > 0 && WIFEXITED(status) && WEXITSTATUS(status) == 0;
  std::string tmp = aof_.path() + ".rewrite";
  if (ok) {
    aof_.flush();
    ok = write_all_file(tmp, captured_, "ab") && std::rename(tmp.c_str(), aof_.path().c_str()) == 0 && aof_.reopen();
  }
  if (!ok) std::remove(tmp.c_str());
  captured_.clear();
  last_status_ = ok;
  return true;
}

void register_rewrite_command(CommandTable& table, AofRewriter& rewriter) {
  table.add({"bgrewriteaof", 1, [&rewriter](CommandContext& c) {
               if (!rewriter.start()) {
                 return resp::append_error(c.reply, "ERR Background append only file rewriting already in progress");
               }
               resp::append_simple_string(c.reply, "Background append only file rewriting started");
             }});
}

}  // namespace rd
