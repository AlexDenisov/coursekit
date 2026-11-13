#include "msg/blocking/blocking.h"

#include <cmath>
#include <cstdlib>

#include "db/lists/lists.h"
#include "resp/encoder/encoder.h"

namespace rd {

bool Blocking::pop(Session& s, std::span<const std::string> args, bool left, bool may_block, std::string& reply) {
  // TODO(you): timeout (strtod on a copy, check it consumed everything), then each key,
  // then wait.
  return true;
}

void Blocking::serve_ready() {
  // TODO(you): for each key in by_key_ whose list is non-empty now...
}

bool Blocking::blocked(const Session& s) const {
  // TODO(you)
  return false;
}

void Blocking::unblock(Session& s) {
  // TODO(you): remove s from by_key_ for each of its keys, cancel the timer, erase,
  // on_unblock.
}

void Blocking::forget(Session& s) {
  // TODO(you): like unblock, without calling on_unblock.
}

void register_blocking_commands(CommandTable& table, Blocking& blocking, std::function<bool(Session*)> may_block) {
  // TODO(you)
}

}  // namespace rd
