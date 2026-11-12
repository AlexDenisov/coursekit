#pragma once

// A small deterministic random number generator (splitmix64). Unlike the
// std:: distributions, it gives the same numbers on every platform and
// standard library, so randomized tests are reproducible: a failure with
// seed 42 fails the same way for everyone.
//
//   rd::Rng rng(42);
//   rng.below(10);         // 0..9, unbiased
//   rng.between(-5, 5);    // -5..5, both ends included
//   rng.uniform();         // [0, 1)
//   rng.coin(0.25);        // true with probability 1/4
//   rng.shuffle(v.begin(), v.end());

#include <cstdint>
#include <iterator>
#include <utility>

#include "common/check.h"

namespace rd {

class Rng {
 public:
  explicit Rng(uint64_t seed) : state_(seed) {}

  uint64_t next() {
    uint64_t z = (state_ += 0x9E3779B97F4A7C15ull);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    return z ^ (z >> 31);
  }

  // Uniform in [0, n), n > 0. Lemire's multiply-and-reject: no modulo bias.
  uint64_t below(uint64_t n) {
    RD_CHECK_GT(n, 0u);
    __uint128_t m = static_cast<__uint128_t>(next()) * n;
    auto low = static_cast<uint64_t>(m);
    if (low < n) {
      uint64_t threshold = -n % n;
      while (low < threshold) {
        m = static_cast<__uint128_t>(next()) * n;
        low = static_cast<uint64_t>(m);
      }
    }
    return static_cast<uint64_t>(m >> 64);
  }

  // Uniform in [lo, hi], both included.
  int64_t between(int64_t lo, int64_t hi) {
    RD_CHECK_LE(lo, hi);
    uint64_t span = static_cast<uint64_t>(hi) - static_cast<uint64_t>(lo);
    uint64_t r = span == UINT64_MAX ? next() : below(span + 1);
    return static_cast<int64_t>(static_cast<uint64_t>(lo) + r);
  }

  // Uniform in [0, 1), 53 random bits.
  double uniform() { return static_cast<double>(next() >> 11) * 0x1.0p-53; }

  bool coin(double p = 0.5) { return uniform() < p; }

  // Fisher–Yates.
  template <class It>
  void shuffle(It first, It last) {
    auto n = static_cast<uint64_t>(std::distance(first, last));
    for (uint64_t i = n; i > 1; --i) {
      using std::swap;
      swap(first[i - 1], first[below(i)]);
    }
  }

 private:
  uint64_t state_;
};

}  // namespace rd
