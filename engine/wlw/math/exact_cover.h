#pragma once

#include <cstdint>
#include <vector>

namespace wlw::math {

// Exact cover: given a table of 0/1 rows, choose rows so that every column has exactly one 1.
// Many puzzles are exact cover problems: tiling a region with pieces (columns = cells + pieces,
// rows = placements), Sudoku, N-queens... In general it's NP-complete, so it's solved by search.
//
// This is Knuth's Algorithm X (backtracking that always branches on the column with the fewest
// candidates) with Dancing Links: the table is kept as circular doubly-linked lists, so a row or
// column is unlinked in O(1) - and since an unlinked node still knows its old neighbours, it's
// relinked in O(1) too when the search backtracks.
//
//   ExactCover problem(7);                 // 7 columns
//   problem.AddRow({2, 4, 5});             // returns the row's id (0, 1, ...)
//   ...
//   auto result = problem.Solve(2);        // up to 2 solutions: enough to tell "unique" apart
class ExactCover {
 public:
  explicit ExactCover(int columns);

  // A row covering the given columns; returns its id. Duplicate columns are ignored.
  int AddRow(const std::vector<int>& columns);

  int Columns() const { return columns_; }
  int Rows() const { return static_cast<int>(rows_.size()); }
  const std::vector<int>& Row(int row) const { return rows_[row]; }

  struct Result {
    std::vector<std::vector<int>> solutions;  // row ids, in the order they were chosen
    int64_t count = 0;                        // solutions found (== solutions.size())
    int64_t nodes = 0;                        // search steps: a measure of how hard it was
    bool complete = true;                     // false if stopped by a limit before searching everything
  };

  // Searches for solutions. `max_solutions` 0 = all. `node_limit` 0 = none (otherwise gives up,
  // with complete = false, after that many steps). `shuffle_seed` != 0 tries rows in a random
  // (but seed-determined) order, so the first solution found is a random one.
  // `chosen`: rows that must be part of the solution (e.g. pieces already on the board); if they
  // clash with each other there's no solution. They're included in every reported solution.
  Result Solve(int64_t max_solutions = 0, uint32_t shuffle_seed = 0, const std::vector<int>& chosen = {},
               int64_t node_limit = 0) const;

 private:
  int columns_;
  std::vector<std::vector<int>> rows_;
};

}  // namespace wlw::math
