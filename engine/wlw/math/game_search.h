#pragma once

#include <algorithm>
#include <concepts>
#include <cstdint>
#include <limits>
#include <optional>
#include <utility>
#include <vector>

// Game-tree search: choosing a move by looking ahead at what each move leads to.
//
// MINIMAX (two sides with opposite goals). Every position has a value from the point of view of
// the "max" side. Where max moves, a position is worth its BEST child for max; where min moves,
// its WORST. Recursing to a depth and scoring the positions found there with an evaluation
// function (a heuristic: "how good does this look") gives the value of the current position and
// the move that leads to it. Chess engines, checkers, connect-four, and Bluzzle's dealer (which
// plays "min" against the player) all work this way.
//
// ALPHA-BETA PRUNING. Keep two bounds while searching: alpha = what max is already sure to get
// elsewhere, beta = what min is already sure to get. When a position's value can no longer land
// between them, the rest of its moves can't change the answer, so they're skipped (a "cutoff").
// The result is exactly minimax's, usually after visiting a small fraction of the tree,
// especially when good moves are tried first. With values 0/1 it becomes a short-circuiting
// "is there a way to win?" search that stops at the first success.
//
// EXPECTIMAX (games with chance: dice, random pieces). Chance positions are worth the
// probability-weighted average of their outcomes. Averages can't be bounded cheaply, so there's
// no pruning there.
//
// A game is described by a small class with these members (the state is copied freely, so keep
// it small: a bitboard, not a std::vector of cells):
//
//   using State = ...;  using Move = ...;
//   std::vector<Move> Moves(const State&) const;   // empty = the game is over here
//   State Apply(const State&, const Move&) const;
//   double Evaluate(const State&) const;           // from max's point of view
//   bool MaxToMove(const State&) const;
// and, for Expectimax, positions where chance moves instead of a player:
//   bool IsChance(const State&) const;
//   std::vector<std::pair<Move, double>> Outcomes(const State&) const;  // probabilities sum to 1
namespace wlw::math {

template <typename G>
concept SearchGame = requires(const G& game, const typename G::State& state, const typename G::Move& move) {
  { game.Moves(state) } -> std::convertible_to<std::vector<typename G::Move>>;
  { game.Apply(state, move) } -> std::convertible_to<typename G::State>;
  { game.Evaluate(state) } -> std::convertible_to<double>;
  { game.MaxToMove(state) } -> std::convertible_to<bool>;
};

template <typename G>
concept ChanceGame = SearchGame<G> && requires(const G& game, const typename G::State& state) {
  { game.IsChance(state) } -> std::convertible_to<bool>;
  { game.Outcomes(state) } -> std::convertible_to<std::vector<std::pair<typename G::Move, double>>>;
};

struct SearchStats {
  int64_t nodes = 0;    // positions visited
  int64_t cutoffs = 0;  // times alpha-beta skipped the remaining moves of a position
};

inline constexpr double kSearchInfinity = std::numeric_limits<double>::infinity();

// The minimax value of `state`, searching `depth` moves ahead, with alpha-beta pruning.
// Call with the default bounds; they narrow as the search recurses.
template <SearchGame G>
double Minimax(const G& game, const typename G::State& state, int depth, double alpha = -kSearchInfinity,
               double beta = kSearchInfinity, SearchStats* stats = nullptr) {
  if (stats) ++stats->nodes;
  if (depth <= 0) return game.Evaluate(state);
  const std::vector<typename G::Move> moves = game.Moves(state);
  if (moves.empty()) return game.Evaluate(state);  // the game ended here
  const bool max_to_move = game.MaxToMove(state);
  double best = max_to_move ? -kSearchInfinity : kSearchInfinity;
  for (const auto& move : moves) {
    const double value = Minimax(game, game.Apply(state, move), depth - 1, alpha, beta, stats);
    if (max_to_move) {
      best = std::max(best, value);
      alpha = std::max(alpha, best);
    } else {
      best = std::min(best, value);
      beta = std::min(beta, best);
    }
    if (alpha >= beta) {  // the other side would never let the game get here: stop looking
      if (stats) ++stats->cutoffs;
      break;
    }
  }
  return best;
}

// The move minimax prefers at `state` (for whichever side is to move), and its value.
template <SearchGame G>
std::optional<std::pair<typename G::Move, double>> BestMove(const G& game, const typename G::State& state, int depth,
                                                            SearchStats* stats = nullptr) {
  const std::vector<typename G::Move> moves = game.Moves(state);
  if (moves.empty() || depth <= 0) return std::nullopt;
  const bool max_to_move = game.MaxToMove(state);
  double alpha = -kSearchInfinity, beta = kSearchInfinity;
  std::optional<std::pair<typename G::Move, double>> best;
  for (const auto& move : moves) {
    const double value = Minimax(game, game.Apply(state, move), depth - 1, alpha, beta, stats);
    if (!best || (max_to_move ? value > best->second : value < best->second)) best = std::make_pair(move, value);
    if (max_to_move) {
      alpha = std::max(alpha, value);
    } else {
      beta = std::min(beta, value);
    }
  }
  return best;
}

// The expectimax value of `state`: players maximize/minimize, chance positions average.
template <ChanceGame G>
double Expectimax(const G& game, const typename G::State& state, int depth, SearchStats* stats = nullptr) {
  if (stats) ++stats->nodes;
  if (depth <= 0) return game.Evaluate(state);
  if (game.IsChance(state)) {
    const auto outcomes = game.Outcomes(state);
    if (outcomes.empty()) return game.Evaluate(state);
    double total = 0.0;
    for (const auto& [move, probability] : outcomes) {
      total += probability * Expectimax(game, game.Apply(state, move), depth - 1, stats);
    }
    return total;
  }
  const std::vector<typename G::Move> moves = game.Moves(state);
  if (moves.empty()) return game.Evaluate(state);
  const bool max_to_move = game.MaxToMove(state);
  double best = max_to_move ? -kSearchInfinity : kSearchInfinity;
  for (const auto& move : moves) {
    const double value = Expectimax(game, game.Apply(state, move), depth - 1, stats);
    best = max_to_move ? std::max(best, value) : std::min(best, value);
  }
  return best;
}

}  // namespace wlw::math
