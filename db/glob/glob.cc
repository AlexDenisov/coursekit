#include "db/glob/glob.h"

namespace rd {

bool glob_match(std::string_view pattern, std::string_view s) {
  // TODO(you): walk pattern and string together; on '*' try every suffix of s
  // recursively, but once the rest of the pattern fails against a suffix, it fails
  // against every shorter one too: stop there.
  return false;
}

}  // namespace rd
