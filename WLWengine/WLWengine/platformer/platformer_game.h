#pragma once
#include <memory>
#include "wlw.h"
#include "scene/window.h"

namespace wlw::platformer {
    class Game {
    public:
      virtual ~Game() = default;
      virtual void Initialize() = 0;
      virtual void Update(float delta_time) = 0;
      virtual void Render() = 0;
      virtual void Run() = 0;

      static std::unique_ptr<Game> Create(const core::Vector2& window_size, const std::string& title);
    };
}
