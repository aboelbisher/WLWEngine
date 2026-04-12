#include "platformer/platformer_game.h"
#include <iostream>

int main() {
    auto game = wlw::platformer::Game::Create({ 1280, 720 }, "WLWengine 2.5D Platformer");
    game->Run();
    return 0;
}