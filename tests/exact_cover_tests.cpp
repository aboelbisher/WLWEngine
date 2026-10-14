// Tests for wlw::math::ExactCover (Algorithm X with Dancing Links).
#include <algorithm>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "test_helpers.h"
#include "wlw/math/exact_cover.h"

TEST_SUITE_BEGIN("exact_cover");

using wlw::math::ExactCover;

static std::vector<int> Sorted(std::vector<int> v) {
  std::sort(v.begin(), v.end());
  return v;
}

// The example from Knuth's "Dancing Links" paper: 7 columns, rows A..F, one solution {A, D, E}.
TEST_CASE("knuth example") {
  ExactCover problem(7);
  const int a = problem.AddRow({2, 4, 5});
  problem.AddRow({0, 3, 6});
  problem.AddRow({1, 2, 5});
  const int d = problem.AddRow({0, 3});
  const int e = problem.AddRow({1, 6});
  problem.AddRow({3, 4, 6});
  const auto result = problem.Solve();
  CHECK((result.count == 1 && result.complete));
  if (result.count == 1) CHECK(Sorted(result.solutions[0]) == (std::vector<int>{a, d, e}));
  CHECK(result.nodes > 0);
}

TEST_CASE("no solution and limits") {
  ExactCover clash(3);
  clash.AddRow({0, 1});
  clash.AddRow({1, 2});  // column 0 and 2 can't both be covered without covering 1 twice
  CHECK(clash.Solve().count == 0);

  ExactCover many(2);  // 2 columns, each coverable by 3 rows: 3 x 3 = 9 solutions
  for (int i = 0; i < 3; ++i) {
    many.AddRow({0});
    many.AddRow({1});
  }
  CHECK(many.Solve().count == 9);
  const auto first_two = many.Solve(2);
  CHECK((first_two.count == 2 && !first_two.complete));

  ExactCover empty(0);  // nothing to cover: exactly one solution, choosing nothing
  CHECK(empty.Solve().count == 1);
}

TEST_CASE("chosen rows") {
  ExactCover problem(4);
  const int r01 = problem.AddRow({0, 1});
  const int r23 = problem.AddRow({2, 3});
  const int r0 = problem.AddRow({0});
  const int r1 = problem.AddRow({1});
  const int r12 = problem.AddRow({1, 2});
  CHECK(problem.Solve().count == 2);  // {01, 23} and {0, 1, 23}
  auto result = problem.Solve(0, 0, {r0});
  CHECK(result.count == 1);
  if (result.count == 1) CHECK(Sorted(result.solutions[0]) == Sorted({r0, r1, r23}));  // includes the given row
  CHECK(problem.Solve(0, 0, {r01, r12}).count == 0);  // given rows that clash: impossible
}

// A 4x4 Sudoku as exact cover: 64 columns (cell filled, row has digit, column has digit, box has
// digit; 16 each), one row per (cell, digit).
TEST_CASE("sudoku") {
  const int puzzle[4][4] = {{1, 0, 0, 0}, {0, 0, 3, 0}, {0, 4, 0, 0}, {0, 0, 0, 2}};
  ExactCover problem(64);
  std::vector<std::pair<int, int>> what;  // row id -> (cell, digit)
  for (int r = 0; r < 4; ++r) {
    for (int c = 0; c < 4; ++c) {
      for (int d = 0; d < 4; ++d) {
        if (puzzle[r][c] != 0 && puzzle[r][c] != d + 1) continue;  // a clue allows only itself
        const int box = (r / 2) * 2 + c / 2;
        problem.AddRow({r * 4 + c, 16 + r * 4 + d, 32 + c * 4 + d, 48 + box * 4 + d});
        what.push_back({r * 4 + c, d + 1});
      }
    }
  }
  const auto result = problem.Solve(2);
  CHECK((result.count == 1 && result.complete));  // a proper Sudoku: exactly one solution
  if (result.count != 1) return;
  int grid[16] = {};
  for (int row : result.solutions[0]) grid[what[row].first] = what[row].second;
  for (int i = 0; i < 4; ++i) {  // every row and column holds 1..4
    std::set<int> in_row, in_column;
    for (int j = 0; j < 4; ++j) {
      in_row.insert(grid[i * 4 + j]);
      in_column.insert(grid[j * 4 + i]);
    }
    CHECK((in_row.size() == 4 && in_column.size() == 4));
  }
}

// The 12 pentominoes tile a 3x20 rectangle in exactly 2 ways; counting the rectangle's own 4
// symmetries (identity, half turn, two mirrors) that's 8 raw solutions. A classic check that a
// DLX solver is right.
TEST_CASE("pentominoes3x20") {
  const char* pentominoes[12] = {"#####",     "##/##/#.",  "###/#../#..", "##./.##/..#", ".#./###/.#.", "##/#./##",
                                 "####/#...", "####/.#..", "###/.#./.#.", ".##/##./.#.", "##../.###",   "##./.#./.##"};
  const int width = 20, height = 3;
  ExactCover problem(width * height + 12);
  for (int p = 0; p < 12; ++p) {
    std::vector<std::pair<int, int>> base;
    int x = 0, y = 0;
    for (const char* s = pentominoes[p]; *s; ++s) {
      if (*s == '/') {
        x = 0;
        ++y;
        continue;
      }
      if (*s == '#') base.push_back({x, y});
      ++x;
    }
    // All 8 orientations (4 turns x mirror), without duplicates.
    std::set<std::vector<std::pair<int, int>>> orientations;
    for (int mirror = 0; mirror < 2; ++mirror) {
      auto cells = base;
      if (mirror) {
        for (auto& c : cells) c.first = -c.first;
      }
      for (int turn = 0; turn < 4; ++turn) {
        int min_x = 99, min_y = 99;
        for (auto& c : cells) {
          min_x = std::min(min_x, c.first);
          min_y = std::min(min_y, c.second);
        }
        auto normal = cells;
        for (auto& c : normal) c = {c.first - min_x, c.second - min_y};
        std::sort(normal.begin(), normal.end());
        orientations.insert(normal);
        for (auto& c : cells) c = {-c.second, c.first};
      }
    }
    for (const auto& shape : orientations) {
      for (int oy = 0; oy < height; ++oy) {
        for (int ox = 0; ox < width; ++ox) {
          std::vector<int> row{width * height + p};
          bool fits = true;
          for (const auto& c : shape) {
            const int cx = ox + c.first, cy = oy + c.second;
            if (cx >= width || cy >= height) {
              fits = false;
              break;
            }
            row.push_back(cy * width + cx);
          }
          if (fits) problem.AddRow(row);
        }
      }
    }
  }
  const auto result = problem.Solve();
  CHECK(result.count == 8);
  std::printf("  3x20 pentominoes: %lld solutions, %lld search nodes\n", static_cast<long long>(result.count),
              static_cast<long long>(result.nodes));
}

TEST_CASE("shuffle is deterministic") {
  ExactCover many(2);
  for (int i = 0; i < 5; ++i) {
    many.AddRow({0});
    many.AddRow({1});
  }
  const auto a = many.Solve(1, 42), b = many.Solve(1, 42);
  CHECK(a.solutions == b.solutions);  // same seed, same first solution
  std::set<std::vector<int>> firsts;
  for (uint32_t seed = 1; seed <= 20; ++seed) firsts.insert(Sorted(many.Solve(1, seed).solutions[0]));
  CHECK(firsts.size() > 1);  // different seeds find different solutions first
}

TEST_SUITE_END();
