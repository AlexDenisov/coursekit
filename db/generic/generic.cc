#include "db/generic/generic.h"

#include <memory>
#include <string>
#include <vector>

#include "db/glob/glob.h"
#include "ds/dict_scan/dict_scan.h"
#include "resp/encoder/encoder.h"
#include "resp/parser/parser.h"

namespace rd {

void register_generic_commands(CommandTable& table, Keyspace& ks, Rng& rng) {
  // TODO(you): KEYS with ks.entries().for_each + glob_match; SCAN with dict_scan; RENAME
  // by moving the Entry's unique_ptr out (ks.entries().find) before erasing src.
}

}  // namespace rd
