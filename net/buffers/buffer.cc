#include "net/buffers/buffer.h"

#include "common/check.h"

namespace rd {

size_t Buffer::size() const {
  // TODO(you): the live range.
  return 0;
}

std::string_view Buffer::view() const {
  // TODO(you): a view of the live range.
  return {};
}

void Buffer::append(std::string_view data) {
  // TODO(you): prepare, copy, commit.
}

char* Buffer::prepare(size_t n) {
  // TODO(you): if the tail has room, done. Else, if the dead space at the front is at
  // least as big as the live data, slide the live data to the front (count it in
  // moved_); if that's still not enough, grow data_ (at least double it).
  return nullptr;
}

void Buffer::commit(size_t n) {
  // TODO(you): extend the live range.
}

void Buffer::consume(size_t n) {
  RD_CHECK_LE(n, size());
  // TODO(you): advance start_; when everything is consumed, rewind both ends to 0.
}

void Buffer::clear() {
  // TODO(you): empty, keeping the allocation.
}

}  // namespace rd
