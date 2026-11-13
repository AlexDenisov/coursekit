#include "db/encodings/encodings.h"

#include <charconv>
#include <cstdlib>

#include "resp/parser/parser.h"

namespace rd {

// ---- CompactHash ----

bool CompactHash::set(std::string_view field, std::string_view value) {
  // TODO(you): listpack: find the field (at even positions), replace the value after it
  // or append both; check the limits first and convert (copy every pair into dict_) if
  // this write would break them.
  return false;
}

std::optional<std::string> CompactHash::get(std::string_view field) const {
  // TODO(you)
  return std::nullopt;
}

bool CompactHash::remove(std::string_view field) {
  // TODO(you)
  return false;
}

size_t CompactHash::size() const {
  // TODO(you)
  return 0;
}

std::string_view CompactHash::encoding() const { return converted_ ? "hashtable" : "listpack"; }

std::vector<std::pair<std::string, std::string>> CompactHash::items() const {
  // TODO(you)
  return {};
}

// ---- CompactSet ----

bool CompactSet::add(std::string_view member) {
  // TODO(you): the conversion table in encodings.h, then add in the current encoding.
  return false;
}

bool CompactSet::remove(std::string_view member) {
  // TODO(you)
  return false;
}

bool CompactSet::contains(std::string_view member) const {
  // TODO(you)
  return false;
}

size_t CompactSet::size() const {
  // TODO(you)
  return 0;
}

std::string_view CompactSet::encoding() const {
  // TODO(you)
  return "";
}

std::vector<std::string> CompactSet::members() const {
  // TODO(you)
  return {};
}

// ---- CompactZSet (optional) ----

bool CompactZSet::add(std::string_view member, double score) {
  // TODO(you, optional): keep the listpack sorted; convert past the limits.
  return false;
}

bool CompactZSet::remove(std::string_view member) {
  // TODO(you, optional)
  return false;
}

std::optional<double> CompactZSet::score(std::string_view member) const {
  // TODO(you, optional)
  return std::nullopt;
}

size_t CompactZSet::size() const {
  // TODO(you, optional)
  return 0;
}

std::string_view CompactZSet::encoding() const { return converted_ ? "skiplist" : "listpack"; }

std::vector<std::pair<std::string, double>> CompactZSet::items() const {
  // TODO(you, optional)
  return {};
}

}  // namespace rd
