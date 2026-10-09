// Tests for math/gf2: Gaussian elimination over GF(2), checked on Lights Out, whose answers are
// well known.
#include <vector>

#include "check.h"
#include "wlw/math/gf2.h"

using namespace wlw::math;

// Lights Out on an n x n board: pressing (x, y) toggles it and its 4 neighbours. Column j = what
// press j toggles.
static std::vector<BitVector> LightsOut(int n) {
  std::vector<BitVector> columns;
  for (int y = 0; y < n; ++y) {
    for (int x = 0; x < n; ++x) {
      BitVector column(n * n);
      const int dx[] = {0, 1, -1, 0, 0}, dy[] = {0, 0, 0, 1, -1};
      for (int k = 0; k < 5; ++k) {
        const int nx = x + dx[k], ny = y + dy[k];
        if (nx >= 0 && nx < n && ny >= 0 && ny < n) column.Set(ny * n + nx);
      }
      columns.push_back(column);
    }
  }
  return columns;
}

// A x, to check a solution really solves the system.
static BitVector Apply(const std::vector<BitVector>& columns, const BitVector& x, int rows) {
  BitVector result(rows);
  for (int j = 0; j < static_cast<int>(columns.size()); ++j) {
    if (x.Get(j)) result ^= columns[j];
  }
  return result;
}

static void TestBitVector() {
  BitVector v(130);  // three words
  v.Set(0);
  v.Set(64);
  v.Set(129);
  CHECK(v.Count() == 3 && v.Get(129) && !v.Get(128));
  v.Flip(64);
  CHECK(v.Count() == 2 && !v.Get(64));
  BitVector w(130);
  w.Set(0);
  CHECK((v ^ w).Count() == 1 && (v ^ v).Count() == 0 && !(v ^ v).Any());
}

static void TestLightsOut3x3() {
  const auto columns = LightsOut(3);
  BitVector all(9);
  for (int i = 0; i < 9; ++i) all.Set(i);
  const auto solution = SolveGf2(columns, all);
  CHECK(solution && solution->rank == 9 && solution->null_space.empty());  // every pattern solvable
  if (solution) CHECK(Apply(columns, solution->x, 9) == all);
}

static void TestLightsOut5x5() {
  const auto columns = LightsOut(5);
  BitVector all(25);
  for (int i = 0; i < 25; ++i) all.Set(i);
  const auto solution = SolveGf2(columns, all);
  CHECK(solution && solution->rank == 23 && solution->null_space.size() == 2);
  if (!solution) return;
  CHECK(Apply(columns, solution->x, 25) == all);
  for (const BitVector& v : solution->null_space) CHECK(!Apply(columns, v, 25).Any());  // they change nothing
  const BitVector lightest = LightestSolution(*solution);
  CHECK(lightest.Count() == 15);  // the famous minimum for "all lights on"
  CHECK(Apply(columns, lightest, 25) == all);
  // Only a quarter of all patterns can be solved on 5x5; a single lit corner can't.
  BitVector corner(25);
  corner.Set(0);
  CHECK(!SolveGf2(columns, corner).has_value());
}

static void TestInconsistentAndSampled() {
  // One move that flips cells 0 and 1 can't light only cell 0.
  BitVector move(2);
  move.Set(0);
  move.Set(1);
  BitVector target(2);
  target.Set(0);
  CHECK(!SolveGf2({move}, target).has_value());
  // Above the exhaustive limit, sampling still returns a valid (light) solution.
  const auto columns = LightsOut(5);
  BitVector all(25);
  for (int i = 0; i < 25; ++i) all.Set(i);
  const auto solution = SolveGf2(columns, all);
  if (solution) {
    const BitVector sampled = LightestSolution(*solution, /*max_exhaustive=*/0, /*samples=*/200);
    CHECK(Apply(columns, sampled, 25) == all && sampled.Count() >= 15);
  }
}

int main() {
  TestBitVector();
  TestLightsOut3x3();
  TestLightsOut5x5();
  TestInconsistentAndSampled();
  return TestResult("gf2");
}
