// Unit tests for the Snake rules. No engine, no window, no GPU: possible only because the rules
// live in SnakeGame, separate from rendering. Run: ctest (or the snake_game_tests executable).
#include <cstdio>
#include <cstdlib>
#include <set>

#include "../src/snake_game.h"

using snake::Cell;
using snake::SnakeGame;
using Result = SnakeGame::StepResult;

static int g_failures = 0;
#define CHECK(cond)                                                    \
  do {                                                                 \
    if (!(cond)) {                                                     \
      std::printf("  FAILED: %s (line %d)\n", #cond, __LINE__);        \
      ++g_failures;                                                    \
    }                                                                  \
  } while (0)

static void TestMovesForward() {
  SnakeGame g(10);
  g.SetFoodForTest({0, 0});
  const Cell head = g.Body().front();
  CHECK(g.Step() == Result::Moved);
  CHECK(g.Body().front() == (Cell{head.x + 1, head.z}));
  CHECK(g.Body().size() == 3);
  CHECK(g.PreviousBody().front() == head);  // the view interpolates previous -> current
}

static void TestEatingGrowsAndScores() {
  SnakeGame g(10);
  const Cell head = g.Body().front();
  g.SetFoodForTest({head.x + 1, head.z});
  CHECK(g.Step() == Result::Ate);
  CHECK(g.Body().size() == 4);
  CHECK(g.Score() == 1);
  CHECK(g.Food() != g.Body().front());  // new food spawned somewhere else
}

static void TestNoReverseAndBuffering() {
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

static void TestWallKills() {
  SnakeGame g(5);
  g.SetBodyForTest({{4, 2}, {3, 2}, {2, 2}}, {1, 0});
  g.SetFoodForTest({0, 0});
  CHECK(g.Step() == Result::Died);
  CHECK(g.GetState() == SnakeGame::State::GameOver);
  CHECK(g.Body().front() == (Cell{4, 2}));  // didn't move into the wall
}

static void TestSelfCollisionKills() {
  // Head at (2,2) moving down into (2,3), which is part of the body (not the tail).
  SnakeGame g(10);
  g.SetBodyForTest({{2, 2}, {3, 2}, {3, 3}, {2, 3}, {1, 3}}, {0, 1});
  g.SetFoodForTest({9, 9});
  CHECK(g.Step() == Result::Died);
}

static void TestMovingIntoTailCellIsLegal() {
  // A 2x2 loop: the head moves into the cell the tail is leaving on the same step.
  SnakeGame g(10);
  g.SetBodyForTest({{2, 2}, {3, 2}, {3, 3}, {2, 3}}, {0, 1});
  g.SetFoodForTest({9, 9});
  CHECK(g.Step() == Result::Moved);
}

static void TestFoodNeverOnSnake() {
  for (uint32_t seed = 0; seed < 200; ++seed) {
    SnakeGame g(6, seed);
    std::set<std::pair<int, int>> body;
    for (const Cell& c : g.Body()) body.insert({c.x, c.z});
    CHECK(body.count({g.Food().x, g.Food().z}) == 0);
  }
}

static void TestSameSeedSameGame() {
  SnakeGame a(10, 42), b(10, 42), c(10, 7);
  CHECK(a.Food() == b.Food());  // deterministic
  bool any_different = false;
  for (uint32_t s = 1; s < 20 && !any_different; ++s) any_different = SnakeGame(10, s).Food() != a.Food();
  CHECK(any_different);
  (void)c;
}

static void TestSpeedsUp() {
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

static void TestResetRestoresStart() {
  SnakeGame g(5);
  g.SetBodyForTest({{4, 2}, {3, 2}, {2, 2}}, {1, 0});
  g.Step();  // dies
  g.Reset(1);
  CHECK(g.GetState() == SnakeGame::State::Playing);
  CHECK(g.Score() == 0 && g.Body().size() == 3);
}

int main() {
  struct { const char* name; void (*fn)(); } tests[] = {
      {"moves forward", TestMovesForward},
      {"eating grows and scores", TestEatingGrowsAndScores},
      {"no reverse + turn buffering", TestNoReverseAndBuffering},
      {"wall kills", TestWallKills},
      {"self collision kills", TestSelfCollisionKills},
      {"moving into the tail's cell is legal", TestMovingIntoTailCellIsLegal},
      {"food never spawns on the snake", TestFoodNeverOnSnake},
      {"same seed, same game", TestSameSeedSameGame},
      {"speeds up with score", TestSpeedsUp},
      {"reset", TestResetRestoresStart},
  };
  for (const auto& t : tests) {
    const int before = g_failures;
    t.fn();
    std::printf("%s %s\n", g_failures == before ? "[ OK ]" : "[FAIL]", t.name);
  }
  std::printf("%s\n", g_failures == 0 ? "All tests passed." : "Some tests FAILED.");
  return g_failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
