#include "wlw/math/gf2.h"

#include <bit>
#include <utility>

#include "wlw/core/random.h"

namespace wlw::math {

int BitVector::Count() const {
  int count = 0;
  for (uint64_t word : words_) count += std::popcount(word);
  return count;
}

bool BitVector::Any() const {
  for (uint64_t word : words_) {
    if (word != 0) return true;
  }
  return false;
}

std::optional<Gf2Solution> SolveGf2(const std::vector<BitVector>& columns, const BitVector& target) {
  const int unknowns = static_cast<int>(columns.size());
  const int equations = target.Size();
  // The augmented matrix [A | b], one row per equation: unknowns + 1 bits.
  std::vector<BitVector> rows(equations, BitVector(unknowns + 1));
  for (int j = 0; j < unknowns; ++j) {
    for (int i = 0; i < equations; ++i) {
      if (columns[j].Get(i)) rows[i].Set(j);
    }
  }
  for (int i = 0; i < equations; ++i) rows[i].Set(unknowns, target.Get(i));

  // Gauss-Jordan elimination: for each column, find a row with a 1 there (the pivot), move it up,
  // and XOR it into every other row with a 1 in that column. Over GF(2) there's no division: a
  // pivot is always 1, and "subtracting a multiple" is just XOR.
  std::vector<int> pivot_column_of_row;
  int rank = 0;
  for (int column = 0; column < unknowns && rank < equations; ++column) {
    int pivot = -1;
    for (int i = rank; i < equations; ++i) {
      if (rows[i].Get(column)) {
        pivot = i;
        break;
      }
    }
    if (pivot < 0) continue;  // a free unknown: no equation pins it down
    std::swap(rows[rank], rows[pivot]);
    for (int i = 0; i < equations; ++i) {
      if (i != rank && rows[i].Get(column)) rows[i] ^= rows[rank];
    }
    pivot_column_of_row.push_back(column);
    ++rank;
  }
  // Rows below the rank are all-zero on the left; a 1 on the right means 0 = 1: no solution.
  for (int i = rank; i < equations; ++i) {
    if (rows[i].Get(unknowns)) return std::nullopt;
  }

  Gf2Solution solution;
  solution.rank = rank;
  solution.x = BitVector(unknowns);
  std::vector<bool> is_pivot(unknowns, false);
  for (int r = 0; r < rank; ++r) {
    is_pivot[pivot_column_of_row[r]] = true;
    solution.x.Set(pivot_column_of_row[r], rows[r].Get(unknowns));  // free unknowns = 0
  }
  // One null-space vector per free unknown f: set f = 1, then each pivot unknown takes whatever
  // makes its equation hold (the 1s in column f of the reduced rows).
  for (int f = 0; f < unknowns; ++f) {
    if (is_pivot[f]) continue;
    BitVector v(unknowns);
    v.Set(f);
    for (int r = 0; r < rank; ++r) {
      if (rows[r].Get(f)) v.Set(pivot_column_of_row[r]);
    }
    solution.null_space.push_back(std::move(v));
  }
  return solution;
}

BitVector LightestSolution(const Gf2Solution& solution, int max_exhaustive, int samples, uint64_t seed) {
  const int k = static_cast<int>(solution.null_space.size());
  BitVector best = solution.x;
  if (k == 0) return best;
  if (k <= max_exhaustive) {
    // Gray code: consecutive combinations differ in one basis vector, so each step is one XOR.
    BitVector current = solution.x;
    for (uint64_t i = 1; i < (uint64_t{1} << k); ++i) {
      current ^= solution.null_space[std::countr_zero(i)];  // the bit that changes from i-1 to i
      if (current.Count() < best.Count()) best = current;
    }
    return best;
  }
  core::Random random(seed);
  for (int s = 0; s < samples; ++s) {
    BitVector candidate = solution.x;
    for (const BitVector& v : solution.null_space) {
      if (random.Chance(0.5f)) candidate ^= v;
    }
    if (candidate.Count() < best.Count()) best = candidate;
  }
  return best;
}

}  // namespace wlw::math
