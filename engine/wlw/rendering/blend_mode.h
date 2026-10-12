#pragma once

namespace wlw::rendering {

// How a drawn pixel combines with the one already there. Every engine shader outputs
// premultiplied alpha (rgb already multiplied by a), so one table serves 3D, canvas and custom
// shaders:
//   Opaque    replaces it (no blending)            solid objects
//   Alpha     new + old * (1 - a)                   glass, ghosts, fades
//   Additive  new + old                             light: glow, sparks, dots that add up (order-free)
//   Multiply  new * old + old * (1 - a)             darkening: fake shadows, tints
// In 3D, anything not Opaque is drawn after the opaque objects, back to front, and doesn't write
// depth by default (see Material::DepthWrite and render_list.h).
enum class BlendMode { Opaque, Alpha, Additive, Multiply };

}  // namespace wlw::rendering
