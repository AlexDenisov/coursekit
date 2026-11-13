#include "ds/listpack/listpack.h"

#include "common/check.h"
#include "resp/parser/parser.h"

namespace rd {

// Suggested private helpers (free functions here): encode one value into
// <encoding+data>; append a backlen; decode the entry starting at an offset (its value
// and total size); read a backlen ending at an offset.

Listpack::Listpack() {
  // TODO(you): total 7, count 0, 0xFF.
}

size_t Listpack::size() const {
  // TODO(you): the header count (walk if it's 65535).
  return 0;
}

void Listpack::push_back(std::string_view value) { insert(size(), value); }

void Listpack::insert(size_t index, std::string_view value) {
  RD_CHECK_LE(index, size());
  // TODO(you): find the byte offset of entry `index` (or of the terminator), splice the
  // new entry in, update total and count.
}

void Listpack::erase(size_t index) {
  RD_CHECK_LT(index, size());
  // TODO(you): find the entry, cut it out, update the header.
}

std::string Listpack::get(size_t index) const {
  RD_CHECK_LT(index, size());
  // TODO(you): walk to it and decode.
  return "";
}

std::optional<size_t> Listpack::find(std::string_view value) const {
  // TODO(you): walk and compare.
  return std::nullopt;
}

std::vector<std::string> Listpack::to_vector() const {
  // TODO(you): walk forwards.
  return {};
}

std::vector<std::string> Listpack::to_vector_reversed() const {
  // TODO(you): start at the terminator; repeatedly read the backlen just before your
  // position and jump back over that entry.
  return {};
}

std::optional<Listpack> Listpack::from_bytes(std::string_view b) {
  // TODO(you): validate as the header describes.
  return std::nullopt;
}

}  // namespace rd
