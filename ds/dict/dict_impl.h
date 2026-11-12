#pragma once

// Your implementation of ds/dict/dict.h. Included at the end of dict.h; don't include
// it yourself.

namespace rd {

template <class V>
typename Dict<V>::Entry* Dict<V>::find_entry(std::string_view key) const {
  // TODO(you): look in table 0, then (if rehashing) table 1: hash & (size - 1), walk the
  // chain.
  return nullptr;
}

template <class V>
V* Dict<V>::find(std::string_view key) {
  // TODO(you): a rehash step if rehashing, then find_entry.
  return nullptr;
}

template <class V>
const V* Dict<V>::find(std::string_view key) const {
  Entry* e = find_entry(key);
  return e ? &e->value : nullptr;
}

template <class V>
std::pair<V*, bool> Dict<V>::insert(std::string key, V value) {
  // TODO(you): rehash step; existing? return it. Else grow_if_needed, then link a new
  // Entry at the head of its bucket in table 1 if rehashing, else table 0.
  return {nullptr, false};
}

template <class V>
V& Dict<V>::get_or_insert(std::string_view key) {
  // TODO(you): find, else insert a default V.
  static V dummy{};
  return dummy;
}

template <class V>
bool Dict<V>::erase(std::string_view key) {
  // TODO(you): rehash step; unlink from whichever table holds it; shrink_if_needed.
  return false;
}

template <class V>
void Dict<V>::clear() {
  // TODO(you): drop both tables, reset the counters and the rehash index.
}

template <class V>
template <class F>
void Dict<V>::for_each(F&& fn) {
  // TODO(you): every bucket of both tables, every entry of each chain.
}

template <class V>
template <class F>
void Dict<V>::for_each(F&& fn) const {
  // TODO(you): the same, const.
}

template <class V>
const std::string* Dict<V>::random_key(Rng& rng) const {
  // TODO(you): random buckets until a non-empty one (while rehashing, skip table 0's
  // already-migrated buckets [0, rehash_index_)), then a random entry of that chain.
  return nullptr;
}

template <class V>
bool Dict<V>::rehash_step(size_t n) {
  // TODO(you): Redis's dictRehash. Move whole chains from tables_[0][rehash_index_] to
  // their buckets in table 1; when used_[0] reaches 0, move table 1 into table 0.
  return false;
}

template <class V>
template <class F>
void Dict<V>::for_each_in_bucket(int t, size_t index, F&& fn) const {
  // TODO(you): walk one chain.
}

template <class V>
void Dict<V>::start_rehash(size_t buckets) {
  // TODO(you): allocate table 1 (or, if table 0 isn't allocated yet, table 0 directly).
}

template <class V>
void Dict<V>::grow_if_needed() {
  // TODO(you): the grow rule from dict.h (and the very first allocation).
}

template <class V>
void Dict<V>::shrink_if_needed() {
  // TODO(you): the shrink rule from dict.h.
}

}  // namespace rd
