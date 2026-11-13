#include "persist/bgsave/bgsave.h"

#include <sys/wait.h>
#include <unistd.h>

#include "persist/rdb_writer/rdb_writer.h"
#include "resp/encoder/encoder.h"

namespace rd {

bool BackgroundSaver::start() {
  // TODO(you): fork; in the child: save_snapshot, then _exit (not exit!); in the parent:
  // remember the pid and add the polling timer.
  return false;
}

bool BackgroundSaver::poll() {
  // TODO(you): waitpid(child_, &status, WNOHANG); WIFEXITED && WEXITSTATUS == 0 is success.
  return false;
}

bool BackgroundSaver::save_now() {
  // TODO(you)
  return false;
}

void register_save_commands(CommandTable& table, BackgroundSaver& saver) {
  // TODO(you): SAVE, BGSAVE, LASTSAVE.
}

}  // namespace rd
