#pragma once

#include <memory>

#include <glm/glm.hpp>

#include "wlw/ui/canvas.h"

namespace wlw::ui {

// Draws a Canvas's quads with the GPU: over the 3D scene, alpha-blended, no depth test.
// Created by Application once the GL context exists.
class UIRenderer {
 public:
  virtual ~UIRenderer() = default;
  virtual void Draw(const Canvas& canvas, glm::vec2 framebuffer_pixels) = 0;
  static std::unique_ptr<UIRenderer> Create();
};

}  // namespace wlw::ui
