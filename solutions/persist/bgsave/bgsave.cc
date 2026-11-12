#include "persist/bgsave/bgsave.h"

#include <sys/wait.h>
#include <unistd.h>

#include "persist/rdb_writer/rdb_writer.h"
#include "resp/encoder/encoder.h"

namespace rd {

bool BackgroundSaver::start() {
  if (in_progress()) return false;
  pid_t pid = ::fork();
  if (pid < 0) return false;
  if (pid == 0) {
    bool ok = rdb::save_snapshot(ks_, path_);
    ::_exit(ok ? 0 : 1);
  }
  child_ = pid;
  loop_.timers().add(ks_.clock().now_ms() + 10, [this](TimerId) -> int64_t {
    poll();
    return in_progress() ? 10 : kNoMore;
  });
  return true;
}

bool BackgroundSaver::poll() {
  if (!in_progress()) return false;
  int status = 0;
  pid_t r = ::waitpid(child_, &status, WNOHANG);
  if (r == 0) return false;
  child_ = -1;
  bool ok = r > 0 && WIFEXITED(status) && WEXITSTATUS(status) == 0;
  last_status_ = ok;
  if (ok) last_save_ms_ = ks_.clock().now_ms();
  return true;
}

bool BackgroundSaver::save_now() {
  if (in_progress()) return false;
  bool ok = rdb::save_snapshot(ks_, path_);
  last_status_ = ok;
  if (ok) last_save_ms_ = ks_.clock().now_ms();
  return ok;
}

void register_save_commands(CommandTable& table, BackgroundSaver& saver) {
  table.add({"save", 1, [&saver](CommandContext& c) {
               if (saver.in_progress()) return resp::append_error(c.reply, "ERR Background save already in progress");
               if (!saver.save_now()) return resp::append_error(c.reply, "ERR");
               resp::append_simple_string(c.reply, "OK");
             }});
  table.add({"bgsave", 1, [&saver](CommandContext& c) {
               if (!saver.start()) return resp::append_error(c.reply, "ERR Background save already in progress");
               resp::append_simple_string(c.reply, "Background saving started");
             }});
  table.add({"lastsave", 1, [&saver](CommandContext& c) { resp::append_integer(c.reply, saver.last_save_ms() / 1000); }});
}

}  // namespace rd
