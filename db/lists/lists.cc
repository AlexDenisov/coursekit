#include "db/lists/lists.h"

#include <memory>

#include "resp/encoder/encoder.h"
#include "resp/parser/parser.h"

namespace rd {

void register_list_commands(CommandTable& table, Keyspace& ks) {
  // TODO(you): one command at a time, with lookup<ListValue>. Write the LRANGE index
  // normalisation once and reuse it in LTRIM. After removing elements, call
  // ks.erase_if_empty(key).
}

void register_list_optional_commands(CommandTable& table, Keyspace& ks) {
  // TODO(you, optional): LREM and LINSERT.
}

}  // namespace rd
