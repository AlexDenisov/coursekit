#pragma once

// repl/backlog: the replication backlog, a ring buffer addressed by replication offset.
// The spec is this file; the story is README.md.

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace rd {

// A primary numbers every byte of its replication stream: the first byte ever sent has
// offset 1, and offset() is the offset of the last one (Redis's master_repl_offset).
// The backlog keeps the most recent `capacity` bytes of that stream.
class Backlog {
 public:
  explicit Backlog(size_t capacity);  // capacity > 0 (checked)

  // Appends to the stream. Bytes older than the last `capacity` are forgotten. O(|data|).
  void append(std::string_view data);

  uint64_t offset() const { return offset_; }
  size_t size() const { return size_; }  // bytes kept
  // The offset of the oldest byte kept (offset() - size() + 1).
  uint64_t first_offset() const { return offset_ - size_ + 1; }

  // PSYNC: a replica asks for the stream from offset `next` on (its own offset + 1).
  // Returns bytes [next, offset()] (empty if next == offset() + 1: it's up to date), or
  // nullopt if they're not all kept: next < first_offset(), next > offset() + 1, or
  // nothing was ever appended. nullopt means "full resync".
  std::optional<std::string> since(uint64_t next) const;

 private:
  std::string ring_;       // capacity bytes
  size_t head_ = 0;        // where the next byte goes
  size_t size_ = 0;
  uint64_t offset_ = 0;
};

}  // namespace rd
