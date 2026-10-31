#include "wlw/studio/studio.h"

// wlw_studio: the Studio with the engine's types only (nothing to play). A game's Studio is its own executable,
// <game>_studio (wlw_add_game(... STUDIO ...)), started by the game's WLW_MAIN.
int main(int argc, char** argv) { return wlw::studio::RunStudio(argc, argv, nullptr, nullptr); }
