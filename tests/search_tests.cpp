// Tests for math::BitGrid (bitboards) and math::game_search (minimax with alpha-beta, expectimax).
#include <utility>
#include <vector>

#include "test_helpers.h"
#include "wlw/math/bit_grid.h"
#include "wlw/math/game_search.h"

TEST_SUITE_BEGIN("search");

using namespace wlw::math;

TEST_CASE("bit grid") {
  using Grid = BitGrid<10, 10>;  // 100 cells: two words, the second partly used
  static_assert(Grid::kWords == 2);
  Grid grid = Grid::Cell(0, 0) | Grid::Cell(9, 9) | Grid::Cell(3, 6);
  CHECK((grid.Count() == 3 && grid.Test(9, 9) && grid.Test(3, 6) && !grid.Test(4, 6)));
  CHECK((!grid.Test(-1, 0) && !grid.Test(10, 0)));  // outside: never set
  CHECK((Grid::Full().Count() == 100 && (~Grid()).Count() == 100));  // padding bits stay clear
  CHECK((~Grid::Full()).None());
  CHECK((Grid::Row(2).Count() == 10 && Grid::Column(7).Count() == 10 && Grid::Row(2, 6).Count() == 6));
  CHECK((Grid::Rect(1, 1, 3, 2).Count() == 6 && Grid::Rect(1, 1, 3, 2).Test(3, 2)));
  CHECK((Grid::Full().Contains(grid) && !grid.Contains(Grid::Full())));

  // Shifting moves cells and drops what falls off any edge (no wrapping into the next row).
  const Grid l = Grid::Cell(0, 0) | Grid::Cell(0, 1) | Grid::Cell(1, 1);
  const Grid moved = l.Shifted(8, 3);
  CHECK((moved.Count() == 3 && moved.Test(8, 3) && moved.Test(9, 4)));
  CHECK(l.Shifted(9, 0).Count() == 2);  // (1,1) would land on x = 10: gone, not wrapped to (0,2)
  CHECK(l.Shifted(0, 9).Count() == 1);

  // Fitting a piece is one AND.
  const Grid occupied = Grid::Row(4);
  CHECK((l.Shifted(2, 2) & occupied).None());
  CHECK((l.Shifted(2, 3) & occupied).Any());
  Grid x = grid;
  x ^= Grid::Cell(0, 0);
  CHECK((x.Count() == 2 && !x.Test(0, 0)));
}

// ---- Tic-tac-toe: small enough to search completely ----
struct TicTacToe {
  using Board = BitGrid<3, 3>;
  struct State {
    Board x, o;
  };
  using Move = int;  // cell 0..8

  static int Winner(const State& s) {
    static const Board lines[] = {Board::Row(0),        Board::Row(1),        Board::Row(2),
                                  Board::Column(0),     Board::Column(1),     Board::Column(2),
                                  Board::Cell(0, 0) | Board::Cell(1, 1) | Board::Cell(2, 2),
                                  Board::Cell(2, 0) | Board::Cell(1, 1) | Board::Cell(0, 2)};
    for (const Board& line : lines) {
      if (s.x.Contains(line)) return 1;
      if (s.o.Contains(line)) return -1;
    }
    return 0;
  }
  std::vector<Move> Moves(const State& s) const {
    std::vector<Move> moves;
    if (Winner(s) != 0) return moves;
    for (int i = 0; i < 9; ++i) {
      if (!(s.x | s.o).Test(i % 3, i / 3)) moves.push_back(i);
    }
    return moves;
  }
  State Apply(const State& s, Move m) const {
    State next = s;
    (MaxToMove(s) ? next.x : next.o).Set(m % 3, m / 3);
    return next;
  }
  double Evaluate(const State& s) const { return Winner(s); }  // +1 X wins, -1 O wins, 0 otherwise
  bool MaxToMove(const State& s) const { return s.x.Count() == s.o.Count(); }  // X starts
};

TEST_CASE("minimax tic tac toe") {
  const TicTacToe game;
  SearchStats stats;
  CHECK(Minimax(game, TicTacToe::State{}, 9, -kSearchInfinity, kSearchInfinity, &stats) == 0.0);  // perfect play: draw
  // The full game tree has 549,946 positions; alpha-beta proves the draw seeing a fraction.
  CHECK((stats.cutoffs > 0 && stats.nodes < 549946 / 5));

  // X to move with two in a row: the search takes the win.
  TicTacToe::State s;
  s.x = TicTacToe::Board::Cell(0, 0) | TicTacToe::Board::Cell(1, 0);
  s.o = TicTacToe::Board::Cell(0, 1) | TicTacToe::Board::Cell(1, 1);
  const auto best = BestMove(game, s, 9);
  CHECK((best && best->first == 2 && best->second == 1.0));
  // O to move in the same spot must block at cell 2 (else X wins next move).
  s.x = TicTacToe::Board::Cell(0, 0) | TicTacToe::Board::Cell(1, 0) | TicTacToe::Board::Cell(2, 2);
  s.o = TicTacToe::Board::Cell(1, 1) | TicTacToe::Board::Cell(0, 2);
  const auto block = BestMove(game, s, 9);
  CHECK((block && block->first == 2));
}

// ---- Expectimax: a safe choice against a gamble ----
struct Gamble {
  enum Phase { kChoose, kCoin, kDone };
  struct State {
    Phase phase = kChoose;
    double payout = 0.0;
  };
  using Move = int;  // choose: 0 safe, 1 gamble; coin: 0 heads, 1 tails
  double win = 10.0, lose = -2.0, safe = 3.0;

  std::vector<Move> Moves(const State& s) const { return s.phase == kChoose ? std::vector<Move>{0, 1} : std::vector<Move>{}; }
  State Apply(const State& s, Move m) const {
    if (s.phase == kChoose) return m == 0 ? State{kDone, safe} : State{kCoin, 0.0};
    return State{kDone, m == 0 ? win : lose};
  }
  double Evaluate(const State& s) const { return s.payout; }
  bool MaxToMove(const State&) const { return true; }
  bool IsChance(const State& s) const { return s.phase == kCoin; }
  std::vector<std::pair<Move, double>> Outcomes(const State&) const { return {{0, 0.5}, {1, 0.5}}; }
};

TEST_CASE("expectimax") {
  Gamble game;
  CHECK(Near(static_cast<float>(Expectimax(game, Gamble::State{}, 3)), 4.0f));  // 0.5*10 + 0.5*-2 = 4 > 3
  game.win = 6.0;                                                                // now 2 < 3: play safe
  CHECK(Near(static_cast<float>(Expectimax(game, Gamble::State{}, 3)), 3.0f));
}

TEST_SUITE_END();
