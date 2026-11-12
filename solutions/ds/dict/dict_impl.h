#pragma once

// Your implementation of ds/dict/dict.h. Included at the end of dict.h; don't include
// it yourself.

namespace rd {

namespace dict_detail {
inline size_t next_power(size_t n) {
  size_t p = 4;
  while (p < n) p *= 2;
  return p;
}
}  // namespace dict_detail

template <class V>
typename Dict<V>::Entry* Dict<V>::find_entry(std::string_view key) const {
  if (size() == 0) return nullptr;
  uint64_t h = hash_(key);
  for (int t = 0; t < 2; ++t) {
    const Table& table = tables_[t];
    if (table.empty()) continue;
    for (Entry* e = table[h & (table.size() - 1)].get(); e != nullptr; e = e->next.get()) {
      if (e->key == key) return e;
    }
    if (!is_rehashing()) break;
  }
  return nullptr;
}

template <class V>
V* Dict<V>::find(std::string_view key) {
  if (is_rehashing()) rehash_step(1);
  Entry* e = find_entry(key);
  return e ? &e->value : nullptr;
}

template <class V>
const V* Dict<V>::find(std::string_view key) const {
  Entry* e = find_entry(key);
  return e ? &e->value : nullptr;
}

template <class V>
std::pair<V*, bool> Dict<V>::insert(std::string key, V value) {
  if (is_rehashing()) rehash_step(1);
  if (Entry* e = find_entry(key)) return {&e->value, false};
  grow_if_needed();
  int t = is_rehashing() ? 1 : 0;
  Table& table = tables_[t];
  size_t idx = hash_(key) & (table.size() - 1);
  auto e = std::make_unique<Entry>(Entry{std::move(key), std::move(value), std::move(table[idx])});
  table[idx] = std::move(e);
  ++used_[t];
  return {&table[idx]->value, true};
}

template <class V>
V& Dict<V>::get_or_insert(std::string_view key) {
  if (V* v = find(key)) return *v;
  return *insert(std::string(key), V{}).first;
}

template <class V>
bool Dict<V>::erase(std::string_view key) {
  if (size() == 0) return false;
  if (is_rehashing()) rehash_step(1);
  uint64_t h = hash_(key);
  for (int t = 0; t < 2; ++t) {
    Table& table = tables_[t];
    if (table.empty()) continue;
    std::unique_ptr<Entry>* link = &table[h & (table.size() - 1)];
    while (*link) {
      if ((*link)->key == key) {
        *link = std::move((*link)->next);
        --used_[t];
        shrink_if_needed();
        return true;
      }
      link = &(*link)->next;
    }
    if (!is_rehashing()) break;
  }
  return false;
}

template <class V>
void Dict<V>::clear() {
  tables_[0].clear();
  tables_[1].clear();
  tables_[0].shrink_to_fit();
  tables_[1].shrink_to_fit();
  used_[0] = used_[1] = 0;
  rehash_index_ = -1;
}

template <class V>
template <class F>
void Dict<V>::for_each(F&& fn) {
  for (Table& table : tables_) {
    for (auto& head : table) {
      for (Entry* e = head.get(); e != nullptr; e = e->next.get()) fn(std::as_const(e->key), e->value);
    }
  }
}

template <class V>
template <class F>
void Dict<V>::for_each(F&& fn) const {
  for (const Table& table : tables_) {
    for (const auto& head : table) {
      for (const Entry* e = head.get(); e != nullptr; e = e->next.get()) fn(e->key, e->value);
    }
  }
}

template <class V>
const std::string* Dict<V>::random_key(Rng& rng) const {
  if (size() == 0) return nullptr;
  const Entry* head = nullptr;
  while (head == nullptr) {
    if (is_rehashing()) {
      size_t skip = static_cast<size_t>(rehash_index_);
      size_t span = tables_[0].size() + tables_[1].size() - skip;
      size_t i = skip + rng.below(span);
      head = i >= tables_[0].size() ? tables_[1][i - tables_[0].size()].get() : tables_[0][i].get();
    } else {
      head = tables_[0][rng.below(tables_[0].size())].get();
    }
  }
  size_t len = 0;
  for (const Entry* e = head; e; e = e->next.get()) ++len;
  size_t pick = rng.below(len);
  const Entry* e = head;
  while (pick--) e = e->next.get();
  return &e->key;
}

template <class V>
bool Dict<V>::rehash_step(size_t n) {
  if (!is_rehashing()) return false;
  size_t empty_visits = n * 10;
  Table& from = tables_[0];
  Table& to = tables_[1];
  while (n-- > 0 && used_[0] != 0) {
    while (!from[static_cast<size_t>(rehash_index_)]) {
      ++rehash_index_;
      if (--empty_visits == 0) return true;
    }
    std::unique_ptr<Entry> e = std::move(from[static_cast<size_t>(rehash_index_)]);
    while (e) {
      std::unique_ptr<Entry> next = std::move(e->next);
      size_t idx = hash_(e->key) & (to.size() - 1);
      e->next = std::move(to[idx]);
      to[idx] = std::move(e);
      --used_[0];
      ++used_[1];
      e = std::move(next);
    }
    ++rehash_index_;
  }
  if (used_[0] == 0) {
    tables_[0] = std::move(tables_[1]);
    tables_[1] = Table();
    used_[0] = used_[1];
    used_[1] = 0;
    rehash_index_ = -1;
    return false;
  }
  return true;
}

template <class V>
template <class F>
void Dict<V>::for_each_in_bucket(int t, size_t index, F&& fn) const {
  for (const Entry* e = tables_[t][index].get(); e != nullptr; e = e->next.get()) fn(e->key, e->value);
}

template <class V>
void Dict<V>::start_rehash(size_t buckets) {
  if (tables_[0].empty()) {
    tables_[0].resize(buckets);
    return;
  }
  tables_[1].resize(buckets);
  rehash_index_ = 0;
}

template <class V>
void Dict<V>::grow_if_needed() {
  if (is_rehashing()) return;
  if (tables_[0].empty()) {
    start_rehash(4);
    return;
  }
  if (size() >= tables_[0].size()) start_rehash(dict_detail::next_power(size() + 1));
}

template <class V>
void Dict<V>::shrink_if_needed() {
  if (is_rehashing()) return;
  size_t buckets = tables_[0].size();
  if (buckets > 4 && size() * 8 <= buckets) start_rehash(dict_detail::next_power(size()));
}

}  // namespace rd
