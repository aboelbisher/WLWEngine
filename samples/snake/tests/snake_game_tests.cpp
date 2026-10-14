// Unit tests for the Snake rules. No engine, no window, no GPU: possible only because the rules
// live in SnakeGame, separate from rendering. Run: ctest (or the snake_game_tests executable).
#include <set>

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest.h>

#include "../src/snake_game.h"

using snake::Cell;
using snake::SnakeGame;
using Result = SnakeGame::StepResult;

TEST_CASE("moves forward") {
  SnakeGame g(10);
  g.SetFoodForTest({0, 0});
  const Cell head = g.Body().front();
  CHECK(g.Step() == Result::Moved);
  CHECK(g.Body().front() == (Cell{head.x + 1, head.z}));
  CHECK(g.Body().size() == 3);
  CHECK(g.PreviousBody().front() == head);  // the view interpolates previous -> current
}

TEST_CASE("eating grows and scores") {
  SnakeGame g(10);
  const Cell head = g.Body().front();
  g.SetFoodForTest({head.x + 1, head.z});
  CHECK(g.Step() == Result::Ate);
  CHECK(g.Body().size() == 4);
  CHECK(g.Score() == 1);
  CHECK(g.Food() != g.Body().front());  // new food spawned somewhere else
}

TEST_CASE("no reverse + turn buffering") {
  SnakeGame g(10);
  g.SetFoodForTest({0, 0});
  g.QueueTurn({-1, 0});  // straight back: ignored
  g.Step();
  CHECK(g.Direction() == (Cell{1, 0}));
  // "up then left" inside one tick: both are kept and applied on consecutive steps
  g.QueueTurn({0, -1});
  g.QueueTurn({-1, 0});
  g.Step();
  CHECK(g.Direction() == (Cell{0, -1}));
  g.Step();
  CHECK(g.Direction() == (Cell{-1, 0}));
}

TEST_CASE("wall kills") {
  SnakeGame g(5);
  g.SetBodyForTest({{4, 2}, {3, 2}, {2, 2}}, {1, 0});
  g.SetFoodForTest({0, 0});
  CHECK(g.Step() == Result::Died);
  CHECK(g.GetState() == SnakeGame::State::GameOver);
  CHECK(g.Body().front() == (Cell{4, 2}));  // didn't move into the wall
}

TEST_CASE("self collision kills") {
  // Head at (2,2) moving down into (2,3), which is part of the body (not the tail).
  SnakeGame g(10);
  g.SetBodyForTest({{2, 2}, {3, 2}, {3, 3}, {2, 3}, {1, 3}}, {0, 1});
  g.SetFoodForTest({9, 9});
  CHECK(g.Step() == Result::Died);
}

TEST_CASE("moving into the tail's cell is legal") {
  // A 2x2 loop: the head moves into the cell the tail is leaving on the same step.
  SnakeGame g(10);
  g.SetBodyForTest({{2, 2}, {3, 2}, {3, 3}, {2, 3}}, {0, 1});
  g.SetFoodForTest({9, 9});
  CHECK(g.Step() == Result::Moved);
}

TEST_CASE("food never spawns on the snake") {
  for (uint32_t seed = 0; seed < 200; ++seed) {
    SnakeGame g(6, seed);
    std::set<std::pair<int, int>> body;
    for (const Cell& c : g.Body()) body.insert({c.x, c.z});
    CHECK(body.count({g.Food().x, g.Food().z}) == 0);
  }
}

TEST_CASE("same seed, same game") {
  SnakeGame a(10, 42), b(10, 42), c(10, 7);
  CHECK(a.Food() == b.Food());  // deterministic
  bool any_different = false;
  for (uint32_t s = 1; s < 20 && !any_different; ++s) any_different = SnakeGame(10, s).Food() != a.Food();
  CHECK(any_different);
  (void)c;
}

TEST_CASE("speeds up with score") {
  SnakeGame g(20);
  const float start = g.MoveInterval();
  for (int i = 0; i < 5; ++i) {
    const Cell head = g.Body().front();
    g.SetFoodForTest({head.x + g.Direction().x, head.z + g.Direction().z});
    g.Step();
  }
  CHECK(g.Score() == 5);
  CHECK(g.MoveInterval() < start);
  CHECK(g.MoveInterval() >= 0.07f);
}

TEST_CASE("reset") {
  SnakeGame g(5);
  g.SetBodyForTest({{4, 2}, {3, 2}, {2, 2}}, {1, 0});
  g.Step();  // dies
  g.Reset(1);
  CHECK(g.GetState() == SnakeGame::State::Playing);
  CHECK((g.Score() == 0 && g.Body().size() == 3));
}
