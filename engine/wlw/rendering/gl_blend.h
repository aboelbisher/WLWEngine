#pragma once

#include "wlw/rendering/blend_mode.h"
#include "wlw/rendering/gl.h"

namespace wlw::rendering {

// Sets the GL blend state for a mode (see blend_mode.h; shaders output premultiplied alpha).
inline void ApplyBlendMode(BlendMode blend) {
  switch (blend) {
    case BlendMode::Opaque:
      glDisable(GL_BLEND);
      return;
    case BlendMode::Alpha:
      glEnable(GL_BLEND);
      glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
      return;
    case BlendMode::Additive:
      glEnable(GL_BLEND);
      glBlendFunc(GL_ONE, GL_ONE);
      return;
    case BlendMode::Multiply:
      glEnable(GL_BLEND);
      glBlendFunc(GL_DST_COLOR, GL_ONE_MINUS_SRC_ALPHA);
      return;
  }
}

}  // namespace wlw::rendering
