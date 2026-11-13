#pragma once

// ds/skiplist: Redis's zskiplist (t_zset.c), the ordered half of a sorted set. The spec
// is this file; the story is README.md.

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "common/rng.h"

namespace rd {

// Elements are (score, member) pairs ordered by score, then by member (bytewise, as
// memcmp). The skiplist doesn't check for duplicate members: the sorted set (db/zsets)
// keeps a dict next to it for that.
class SkipList {
 public:
  static constexpr int kMaxLevel = 32;
  static constexpr double kP = 0.25;  // chance of each extra level

  struct Node {
    std::string member;
    double score = 0;
    Node* backward = nullptr;  // previous node at level 0 (nullptr for the first)
    struct Level {
      Node* forward = nullptr;
      size_t span = 0;  // how many level-0 steps `forward` jumps
    };
    std::vector<Level> levels;  // levels.size() is this node's height

    Node* next() const { return levels[0].forward; }
  };

  // `seed` drives the random node heights, so tests are reproducible.
  explicit SkipList(uint64_t seed = 1);
  ~SkipList();
  SkipList(const SkipList&) = delete;
  SkipList& operator=(const SkipList&) = delete;

  size_t size() const { return length_; }
  // Height of the tallest node (1 for an empty list).
  int level() const { return level_; }

  // A random height: 1, then +1 while rng.uniform() < kP, at most kMaxLevel.
  int random_level();

  // Inserts (score, member). Precondition: not already present. Returns the new node.
  // O(log n) expected.
  Node* insert(double score, std::string member);

  // Removes (score, member). False if not present. O(log n) expected.
  bool erase(double score, std::string_view member);

  // Changes the score of the element (old_score, member), which must exist. Returns its
  // node (possibly a new one). O(log n).
  Node* update_score(double old_score, std::string_view member, double new_score);

  // 0-based rank of (score, member), or -1 if absent. O(log n) via the spans.
  int64_t rank(double score, std::string_view member) const;

  // The node at 0-based rank, or nullptr if rank >= size(). O(log n) via the spans.
  Node* at(size_t rank) const;

  // The first node with score >= min (> min if exclusive), or nullptr.
  Node* first_from(double min, bool exclusive) const;
  // The last node with score <= max (< max if exclusive), or nullptr.
  Node* last_until(double max, bool exclusive) const;

  Node* first() const { return header_->levels[0].forward; }
  Node* last() const { return tail_; }

 private:
  Node* header_;  // a sentinel with kMaxLevel levels; not an element
  Node* tail_ = nullptr;
  size_t length_ = 0;
  int level_ = 1;
  Rng rng_;
};

}  // namespace rd
