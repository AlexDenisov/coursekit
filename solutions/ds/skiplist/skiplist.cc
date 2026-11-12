#include "ds/skiplist/skiplist.h"

#include "common/check.h"

namespace rd {

namespace {

bool less(double s1, std::string_view m1, double s2, std::string_view m2) {
  return s1 < s2 || (s1 == s2 && m1 < m2);
}

}  // namespace

SkipList::SkipList(uint64_t seed) : rng_(seed) {
  header_ = new Node;
  header_->levels.resize(kMaxLevel);
}

SkipList::~SkipList() {
  Node* n = header_;
  while (n != nullptr) {
    Node* next = n->levels[0].forward;
    delete n;
    n = next;
  }
}

int SkipList::random_level() {
  int l = 1;
  while (l < kMaxLevel && rng_.uniform() < kP) ++l;
  return l;
}

SkipList::Node* SkipList::insert(double score, std::string member) {
  Node* update[kMaxLevel];
  size_t rank[kMaxLevel];
  Node* x = header_;
  for (int i = level_ - 1; i >= 0; --i) {
    rank[i] = i == level_ - 1 ? 0 : rank[i + 1];
    while (x->levels[i].forward && less(x->levels[i].forward->score, x->levels[i].forward->member, score, member)) {
      rank[i] += x->levels[i].span;
      x = x->levels[i].forward;
    }
    update[i] = x;
  }
  int lvl = random_level();
  if (lvl > level_) {
    for (int i = level_; i < lvl; ++i) {
      rank[i] = 0;
      update[i] = header_;
      update[i]->levels[i].span = length_;
    }
    level_ = lvl;
  }
  Node* n = new Node;
  n->member = std::move(member);
  n->score = score;
  n->levels.resize(static_cast<size_t>(lvl));
  for (int i = 0; i < lvl; ++i) {
    n->levels[i].forward = update[i]->levels[i].forward;
    update[i]->levels[i].forward = n;
    n->levels[i].span = update[i]->levels[i].span - (rank[0] - rank[i]);
    update[i]->levels[i].span = (rank[0] - rank[i]) + 1;
  }
  for (int i = lvl; i < level_; ++i) update[i]->levels[i].span++;
  n->backward = update[0] == header_ ? nullptr : update[0];
  if (n->levels[0].forward) {
    n->levels[0].forward->backward = n;
  } else {
    tail_ = n;
  }
  ++length_;
  return n;
}

bool SkipList::erase(double score, std::string_view member) {
  Node* update[kMaxLevel];
  Node* x = header_;
  for (int i = level_ - 1; i >= 0; --i) {
    while (x->levels[i].forward && less(x->levels[i].forward->score, x->levels[i].forward->member, score, member)) {
      x = x->levels[i].forward;
    }
    update[i] = x;
  }
  x = x->levels[0].forward;
  if (x == nullptr || x->score != score || x->member != member) return false;
  for (int i = 0; i < level_; ++i) {
    if (update[i]->levels[i].forward == x) {
      update[i]->levels[i].span += x->levels[i].span - 1;
      update[i]->levels[i].forward = x->levels[i].forward;
    } else {
      update[i]->levels[i].span -= 1;
    }
  }
  if (x->levels[0].forward) {
    x->levels[0].forward->backward = x->backward;
  } else {
    tail_ = x->backward;
  }
  while (level_ > 1 && header_->levels[level_ - 1].forward == nullptr) --level_;
  --length_;
  delete x;
  return true;
}

SkipList::Node* SkipList::update_score(double old_score, std::string_view member, double new_score) {
  std::string m(member);
  RD_CHECK(erase(old_score, m), "update_score of a missing element");
  return insert(new_score, std::move(m));
}

int64_t SkipList::rank(double score, std::string_view member) const {
  Node* x = header_;
  size_t r = 0;
  for (int i = level_ - 1; i >= 0; --i) {
    while (x->levels[i].forward && !less(score, member, x->levels[i].forward->score, x->levels[i].forward->member)) {
      r += x->levels[i].span;
      x = x->levels[i].forward;
    }
    if (x != header_ && x->score == score && x->member == member) return static_cast<int64_t>(r) - 1;
  }
  return -1;
}

SkipList::Node* SkipList::at(size_t rank) const {
  if (rank >= length_) return nullptr;
  size_t target = rank + 1, traversed = 0;
  Node* x = header_;
  for (int i = level_ - 1; i >= 0; --i) {
    while (x->levels[i].forward && traversed + x->levels[i].span <= target) {
      traversed += x->levels[i].span;
      x = x->levels[i].forward;
    }
    if (traversed == target) return x;
  }
  return nullptr;
}

SkipList::Node* SkipList::first_from(double min, bool exclusive) const {
  Node* x = header_;
  for (int i = level_ - 1; i >= 0; --i) {
    while (x->levels[i].forward &&
           (exclusive ? x->levels[i].forward->score <= min : x->levels[i].forward->score < min)) {
      x = x->levels[i].forward;
    }
  }
  return x->levels[0].forward;
}

SkipList::Node* SkipList::last_until(double max, bool exclusive) const {
  Node* x = header_;
  for (int i = level_ - 1; i >= 0; --i) {
    while (x->levels[i].forward &&
           (exclusive ? x->levels[i].forward->score < max : x->levels[i].forward->score <= max)) {
      x = x->levels[i].forward;
    }
  }
  return x == header_ ? nullptr : x;
}

}  // namespace rd
