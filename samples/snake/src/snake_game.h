#pragma once

#include <cstdint>
#include <deque>
#include <random>
#include <vector>

namespace snake {

struct Cell {
  int x = 0;
  int z = 0;
  bool operator==(const Cell& o) const { return x == o.x && z == o.z; }
  bool operator!=(const Cell& o) const { return !(*this == o); }
};

// The rules of Snake as plain data + logic. Deliberately knows nothing about the engine, the
// renderer or input devices: SnakeApp feeds it turns and reads its state to draw. That keeps
// the rules unit-testable (tests/snake_game_tests.cpp) and deterministic for a given seed.
class SnakeGame {
 public:
  enum class State { Playing, GameOver };
  enum class StepResult { Moved, Ate, Died };

  explicit SnakeGame(int grid_size = 20, uint32_t seed = 12345);

  void Reset(uint32_t seed);

  // Queues a turn (one cell in x or z). Applied one per Step(); 180-degree turns, repeats and
  // more than 3 pending turns are ignored.
  void QueueTurn(Cell direction);

  // Advances the snake one cell. Call this on the game's own tick (see MoveInterval()).
  StepResult Step();

  // ---- State, read by the view ----
  State GetState() const { return state_; }
  int GridSize() const { return grid_size_; }
  const std::deque<Cell>& Body() const { return body_; }                   // body_[0] is the head
  const std::deque<Cell>& PreviousBody() const { return previous_body_; }  // before the last Step()
  Cell Food() const { return food_; }
  int Score() const { return score_; }
  Cell Direction() const { return direction_; }

  // Seconds per cell: starts at 0.15 s and speeds up as the score grows, down to 0.07 s.
  float MoveInterval() const;

  // ---- For tests ----
  void SetBodyForTest(std::deque<Cell> body, Cell direction);
  void SetFoodForTest(Cell food) { food_ = food; }

 private:
  bool InBounds(Cell c) const { return c.x >= 0 && c.x < grid_size_ && c.z >= 0 && c.z < grid_size_; }
  bool Occupies(Cell c, bool ignore_tail) const;
  bool SpawnFood();  // false when the board is full (you won!)

  int grid_size_;
  State state_ = State::Playing;
  std::deque<Cell> body_;
  std::deque<Cell> previous_body_;
  Cell direction_{1, 0};
  std::deque<Cell> turn_queue_;
  Cell food_{0, 0};
  int score_ = 0;
  std::mt19937 rng_;  // seeded: the same seed + the same inputs = the same game
};

}  // namespace snake
