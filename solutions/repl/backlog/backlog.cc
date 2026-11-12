#include "repl/backlog/backlog.h"

#include <algorithm>

#include "common/check.h"

namespace rd {

Backlog::Backlog(size_t capacity) : ring_(capacity, '\0') { RD_CHECK_GT(capacity, 0u); }

void Backlog::append(std::string_view data) {
  offset_ += data.size();
  size_t cap = ring_.size();
  if (data.size() > cap) data = data.substr(data.size() - cap);
  size_t first = std::min(data.size(), cap - head_);
  ring_.replace(head_, first, data.substr(0, first));
  ring_.replace(0, data.size() - first, data.substr(first));
  head_ = (head_ + data.size()) % cap;
  size_ = std::min(cap, size_ + data.size());
}

std::optional<std::string> Backlog::since(uint64_t next) const {
  if (offset_ == 0 || next < first_offset() || next > offset_ + 1) return std::nullopt;
  size_t n = static_cast<size_t>(offset_ + 1 - next);
  size_t cap = ring_.size();
  size_t start = (head_ + cap - n) % cap;
  size_t first = std::min(n, cap - start);
  std::string out = ring_.substr(start, first);
  out += ring_.substr(0, n - first);
  return out;
}

}  // namespace rd
