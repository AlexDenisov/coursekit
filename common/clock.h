#pragma once

// Time, behind an interface so tests can control it.
//
//   rd::SystemClock clock;      // real time: milliseconds since the Unix epoch
//   rd::FakeClock fake(1000);   // starts at 1000 ms; only moves when told to
//   fake.advance(250);

#include <chrono>
#include <cstdint>

namespace rd {

class Clock {
 public:
  virtual ~Clock() = default;
  // Milliseconds since the Unix epoch (EXPIREAT and RDB files store absolute times).
  virtual int64_t now_ms() const = 0;
};

class SystemClock final : public Clock {
 public:
  int64_t now_ms() const override {
    using namespace std::chrono;
    return duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
  }
};

class FakeClock final : public Clock {
 public:
  explicit FakeClock(int64_t start_ms = 1'700'000'000'000) : now_(start_ms) {}
  int64_t now_ms() const override { return now_; }
  void set(int64_t ms) { now_ = ms; }
  void advance(int64_t ms) { now_ += ms; }

 private:
  int64_t now_;
};

}  // namespace rd
