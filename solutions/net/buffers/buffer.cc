#include "net/buffers/buffer.h"

#include <algorithm>
#include <cstring>

#include "common/check.h"

namespace rd {

size_t Buffer::size() const { return end_ - start_; }

std::string_view Buffer::view() const {
  return data_.empty() ? std::string_view() : std::string_view(data_.data() + start_, size());
}

void Buffer::append(std::string_view data) {
  if (data.empty()) return;
  std::memcpy(prepare(data.size()), data.data(), data.size());
  commit(data.size());
}

char* Buffer::prepare(size_t n) {
  if (data_.size() - end_ < n) {
    size_t live = size();
    if (start_ >= live && data_.size() - live >= n) {
      std::memmove(data_.data(), data_.data() + start_, live);
      moved_ += live;
      start_ = 0;
      end_ = live;
    } else {
      size_t want = std::max({data_.size() * 2, end_ + n, size_t{64}});
      data_.resize(want);
    }
  }
  return data_.data() + end_;
}

void Buffer::commit(size_t n) {
  RD_CHECK_LE(end_ + n, data_.size());
  end_ += n;
}

void Buffer::consume(size_t n) {
  RD_CHECK_LE(n, size());
  start_ += n;
  if (start_ == end_) start_ = end_ = 0;
}

void Buffer::clear() { start_ = end_ = 0; }

}  // namespace rd
