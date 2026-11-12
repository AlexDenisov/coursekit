#pragma once

// net/buffers: a byte queue for socket I/O. The spec is this file; the story is
// README.md.

#include <cstddef>
#include <string_view>
#include <vector>

namespace rd {

// Bytes go in at the back (append, or prepare + commit to read() straight into the
// buffer) and come out at the front (view + consume). Every operation is amortised
// O(bytes it touches): consuming from the front never moves the rest each time.
class Buffer {
 public:
  // Readable bytes, front to back.
  size_t size() const;
  bool empty() const { return size() == 0; }
  std::string_view view() const;

  void append(std::string_view data);

  // Makes room for at least n more bytes at the back and returns where to write them.
  // Then call commit(k) with the k <= n bytes actually written. The pointer is valid
  // until the next non-const call.
  char* prepare(size_t n);
  void commit(size_t n);

  // Drops the first n bytes (n <= size(), checked).
  void consume(size_t n);
  void clear();

  // Bytes allocated.
  size_t capacity() const { return data_.size(); }

  // Total bytes ever copied to make room (compaction). The amortisation contract:
  // bytes_moved() <= total bytes ever appended or committed.
  size_t bytes_moved() const { return moved_; }

 private:
  std::vector<char> data_;  // capacity; live bytes are data_[start_, end_)
  size_t start_ = 0;
  size_t end_ = 0;
  size_t moved_ = 0;
};

}  // namespace rd
