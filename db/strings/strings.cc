#include "db/strings/strings.h"

#include "common/check.h"
#include "resp/encoder/encoder.h"
#include "resp/parser/parser.h"

namespace rd {

void register_string_commands(CommandTable& table, StringDb& db) {
  // TODO(you): one table.add per command, each a lambda capturing &db. Write INCRBY once
  // (a helper taking the increment) and call it from INCR, DECR, INCRBY and DECRBY.
  // Overflow: test against INT64_MAX - old / INT64_MIN - old before adding.
}

void register_string_range_commands(CommandTable& table, StringDb& db) {
  // TODO(you, optional): GETRANGE and SETRANGE. Do the 512 MB check before resizing.
}

}  // namespace rd
