#pragma once

#include <cstdint>
#include <optional>
#include <vector>

// Linear algebra over GF(2), the field with two elements: 0 and 1, where + is XOR and * is AND
// (1 + 1 = 0). Vectors are rows of bits.
//
// Problems shaped like "each move flips some cells; which moves turn everything off?" are linear
// systems A x = b over GF(2): one equation per cell (the moves that touch it must add up to its
// state), one unknown per move (used or not: using a move twice cancels out, and the order never
// matters). Lights Out is the classic; Bluzzle's Flip mode is another. Gaussian elimination solves
// them exactly, says when they can't be solved, and gives the null space: the combinations of
// moves that change nothing, from which every other solution follows.
namespace wlw::math {

// A fixed-length vector of bits.
class BitVector {
 public:
  BitVector() = default;
  explicit BitVector(int size) : size_(size), words_((size + 63) / 64, 0) {}

  int Size() const { return size_; }
  bool Get(int i) const { return (words_[i / 64] >> (i % 64)) & 1u; }
  void Set(int i, bool value = true) {
    const uint64_t mask = uint64_t{1} << (i % 64);
    words_[i / 64] = value ? (words_[i / 64] | mask) : (words_[i / 64] & ~mask);
  }
  void Flip(int i) { words_[i / 64] ^= uint64_t{1} << (i % 64); }
  BitVector& operator^=(const BitVector& o) {  // vector addition over GF(2)
    for (size_t w = 0; w < words_.size(); ++w) words_[w] ^= o.words_[w];
    return *this;
  }
  BitVector operator^(const BitVector& o) const {
    BitVector result = *this;
    return result ^= o;
  }
  int Count() const;  // the number of 1s ("weight")
  bool Any() const;
  bool operator==(const BitVector&) const = default;

 private:
  int size_ = 0;
  std::vector<uint64_t> words_;
};

// A x = b over GF(2), with A given column by column: column j = what unknown j does (e.g. which
// cells move j flips), b = the target (e.g. which cells are lit).
struct Gf2Solution {
  BitVector x;                        // one solution (free unknowns set to 0)
  std::vector<BitVector> null_space;  // a basis: x ^ any combination of these is also a solution
  int rank = 0;
};

// Solves by Gaussian elimination. Nothing if there's no solution.
std::optional<Gf2Solution> SolveGf2(const std::vector<BitVector>& columns, const BitVector& target);

// The solution with the fewest 1s (moves), by trying every combination of the null-space basis
// (2^k for k basis vectors): exact when k <= max_exhaustive. Above that, `samples` random
// combinations are tried and the lightest found is returned (good, not guaranteed minimal).
BitVector LightestSolution(const Gf2Solution& solution, int max_exhaustive = 20, int samples = 20000,
                           uint64_t seed = 1);

}  // namespace wlw::math
