#include "game.h"

int main() {
  auto game = wlw::game::Game::Create({1280, 720}, "WLWengine Arena");
  game->Run();
  return 0;
}
