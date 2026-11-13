#include "ds/skiplist/skiplist.h"

#include "common/check.h"

namespace rd {

namespace {

// The order: score, then member bytes.
bool less(double s1, std::string_view m1, double s2, std::string_view m2) {
  return s1 < s2 || (s1 == s2 && m1 < m2);
}

}  // namespace

SkipList::SkipList(uint64_t seed) : rng_(seed) {
  header_ = new Node;
  header_->levels.resize(kMaxLevel);
}

SkipList::~SkipList() {
  // Given: free every node by walking level 0.
  Node* n = header_;
  while (n != nullptr) {
    Node* next = n->levels[0].forward;
    delete n;
    n = next;
  }
}

int SkipList::random_level() {
  // TODO(you): as in skiplist.h.
  return 1;
}

SkipList::Node* SkipList::insert(double score, std::string member) {
  // TODO(you): walk down from level_-1, remembering for each level the last node before
  // the insertion point (update[i]) and its rank (rank[i]). Pick a height; link the new
  // node in at each of its levels, splitting the span of update[i]; add 1 to the spans of
  // the taller levels that jump over it; fix backward and tail_.
  return nullptr;
}

bool SkipList::erase(double score, std::string_view member) {
  // TODO(you): find update[] as in insert; unlink at each level where update[i] points to
  // the node (merging spans), and subtract 1 from the spans elsewhere; fix backward,
  // tail_, level_.
  return false;
}

SkipList::Node* SkipList::update_score(double old_score, std::string_view member, double new_score) {
  // TODO(you): erase + insert is fine (Redis updates in place when the order is unchanged).
  return nullptr;
}

int64_t SkipList::rank(double score, std::string_view member) const {
  // TODO(you): walk as in a search, summing the spans you cross.
  return -1;
}

SkipList::Node* SkipList::at(size_t rank) const {
  // TODO(you): walk down, crossing spans while they don't overshoot rank + 1.
  return nullptr;
}

SkipList::Node* SkipList::first_from(double min, bool exclusive) const {
  // TODO(you): the last node *before* the range, then one step.
  return nullptr;
}

SkipList::Node* SkipList::last_until(double max, bool exclusive) const {
  // TODO(you): the last node still in the range.
  return nullptr;
}

}  // namespace rd
