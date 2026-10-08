#include "snake_game.h"

#include <algorithm>

namespace snake {

SnakeGame::SnakeGame(int grid_size, uint32_t seed) : grid_size_(grid_size) { Reset(seed); }

void SnakeGame::Reset(uint32_t seed) {
  rng_.seed(seed);
  state_ = State::Playing;
  score_ = 0;
  direction_ = {1, 0};
  turn_queue_.clear();

  // Three cells in the middle, heading right: head first.
  const int mid = grid_size_ / 2;
  body_ = {{mid, mid}, {mid - 1, mid}, {mid - 2, mid}};
  previous_body_ = body_;
  SpawnFood();
}

void SnakeGame::QueueTurn(Cell turn) {
  if (state_ != State::Playing) return;
  // Compare with the direction the snake *will* have after the pending turns, not the current one.
  const Cell last = turn_queue_.empty() ? direction_ : turn_queue_.back();
  const bool same = turn == last;
  const bool reverse = turn.x == -last.x && turn.z == -last.z;  // can't turn back into yourself
  if (same || reverse || turn_queue_.size() >= 3) return;
  turn_queue_.push_back(turn);
}

SnakeGame::StepResult SnakeGame::Step() {
  if (state_ != State::Playing) return StepResult::Died;

  if (!turn_queue_.empty()) {
    direction_ = turn_queue_.front();
    turn_queue_.pop_front();
  }

  previous_body_ = body_;
  const Cell next{body_.front().x + direction_.x, body_.front().z + direction_.z};
  const bool eats = next == food_;

  // The tail moves out of its cell this step - unless we're eating (then the snake grows and the
  // tail stays). So moving into the current tail cell is legal when not eating.
  if (!InBounds(next) || Occupies(next, /*ignore_tail=*/!eats)) {
    state_ = State::GameOver;
    previous_body_ = body_;  // freeze in place, don't interpolate into the wall
    return StepResult::Died;
  }

  body_.push_front(next);  // a deque makes both ends O(1): the snake is a queue
  if (eats) {
    ++score_;
    if (!SpawnFood()) state_ = State::GameOver;  // board full
    return StepResult::Ate;
  }
  body_.pop_back();
  return StepResult::Moved;
}

bool SnakeGame::Occupies(Cell c, bool ignore_tail) const {
  const size_t count = ignore_tail ? body_.size() - 1 : body_.size();
  return std::find(body_.begin(), body_.begin() + static_cast<std::ptrdiff_t>(count), c) !=
         body_.begin() + static_cast<std::ptrdiff_t>(count);
}

bool SnakeGame::SpawnFood() {
  // Pick uniformly among the free cells. (Retrying random cells until one is free also works,
  // but gets slower and slower as the snake fills the board.)
  std::vector<Cell> free_cells;
  free_cells.reserve(static_cast<size_t>(grid_size_ * grid_size_));
  for (int z = 0; z < grid_size_; ++z) {
    for (int x = 0; x < grid_size_; ++x) {
      if (!Occupies({x, z}, /*ignore_tail=*/false)) free_cells.push_back({x, z});
    }
  }
  if (free_cells.empty()) return false;
  std::uniform_int_distribution<size_t> pick(0, free_cells.size() - 1);
  food_ = free_cells[pick(rng_)];
  return true;
}

float SnakeGame::MoveInterval() const { return std::max(0.07f, 0.15f - 0.004f * static_cast<float>(score_)); }

void SnakeGame::SetBodyForTest(std::deque<Cell> body, Cell direction) {
  body_ = std::move(body);
  previous_body_ = body_;
  direction_ = direction;
  turn_queue_.clear();
  state_ = State::Playing;
}

}  // namespace snake
