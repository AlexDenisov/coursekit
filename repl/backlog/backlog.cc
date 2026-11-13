#include "repl/backlog/backlog.h"

#include "common/check.h"

namespace rd {

Backlog::Backlog(size_t capacity) : ring_(capacity, '\0') { RD_CHECK_GT(capacity, 0u); }

void Backlog::append(std::string_view data) {
  // TODO(you): advance offset_ by |data|; if data is longer than the ring, only its tail
  // matters; copy in at most two pieces (up to the end of the ring, then from the start).
}

std::optional<std::string> Backlog::since(uint64_t next) const {
  // TODO(you): check the range; the oldest byte is at (head_ - size_) mod capacity; copy
  // out in at most two pieces.
  return std::nullopt;
}

}  // namespace rd
