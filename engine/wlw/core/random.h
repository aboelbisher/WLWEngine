#pragma once

#include <cstdint>

namespace wlw::core {

// A small, fast, deterministic random number generator (SplitMix64).
//
// Deterministic means: the same seed gives the same numbers on every compiler, standard library
// and CPU. std::mt19937 is portable too, but std::uniform_int_distribution and friends are not
// (MSVC and Android's libc++ turn the same raw numbers into different results), which breaks
// anything that must repeat exactly: seeded levels, daily challenges, replays, tests.
//
// The whole state is one 64-bit number, so saving/restoring a generator (to resume a game and
// get exactly the same future) is just State() / SetState().
class Random {
 public:
  explicit Random(uint64_t seed = 0) : state_(seed) {}

  uint32_t Next() {
    uint64_t z = (state_ += 0x9E3779B97F4A7C15ull);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    return static_cast<uint32_t>((z ^ (z >> 31)) >> 32);
  }

  // Uniform-ish in [0, n): a multiply instead of %, portable, with a bias far too small to matter
  // for game-sized n. n <= 0 gives 0.
  int Below(int n) {
    if (n <= 0) return 0;
    return static_cast<int>((static_cast<uint64_t>(Next()) * static_cast<uint64_t>(n)) >> 32);
  }

  float Unit() { return static_cast<float>(Next() >> 8) * (1.0f / 16777216.0f); }  // [0, 1)
  float Range(float min, float max) { return min + (max - min) * Unit(); }
  bool Chance(float probability) { return Unit() < probability; }

  uint64_t State() const { return state_; }
  void SetState(uint64_t state) { state_ = state; }

 private:
  uint64_t state_;
};

}  // namespace wlw::core
