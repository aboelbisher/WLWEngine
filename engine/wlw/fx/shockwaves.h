#pragma once

#include <array>

#include <glm/glm.hpp>

#include "wlw/rendering/post_settings.h"

namespace wlw::fx {

// One shockwave's life. The ring grows from `start_radius` to `end_radius` fast, then slowing
// (OutCubic: a blast front loses speed), and widens from `width` to `end_width`. Its push fades in
// over the first tenth of its life (no pop on the first frame) and out by its end, quadratically:
// most of the bend is early, while the ring is small and fast. Lengths are rendering::Ripple's
// (fractions of the viewport's height).
struct ShockwaveSettings {
  float duration = 0.6f;  // seconds from its start to gone
  float delay = 0.0f;     // seconds before it starts (staggered rings)
  float start_radius = 0.0f;
  float end_radius = 0.5f;
  float width = 0.06f;
  float end_width = 0.12f;
  float strength = 0.02f;  // the largest push (negative: inwards)
  float chroma = 0.0f;     // colour split, as rendering::Ripple::chroma (the same over its life)
};

// The ring a shockwave around `center` draws `age` seconds after it was added: inactive (strength 0)
// before its delay and from its end on.
rendering::Ripple ShockwaveAt(glm::vec2 center, const ShockwaveSettings& settings, float age);

// Shockwaves: rings that grow out of points and bend the picture as they pass (rendering::Ripple,
// drawn by the output pass). The game adds one per impact; this ages and recycles them and writes
// them into a viewport's post settings every frame. At most rendering::kMaxRipples at once: when all
// are busy, a new one takes the place of the one nearest its end.
//
//   shockwaves.Add(screen_point / screen_size, {.end_radius = 0.4f, .strength = 0.015f});
//   shockwaves.Update(dt);                           // every frame, then
//   shockwaves.WriteTo(viewport.Post().Settings());  // the rings into the ripples (the rest off)
class Shockwaves {
 public:
  // `center` in the viewport, 0..1 from its top-left corner (a screen point / the screen's size).
  void Add(glm::vec2 center, const ShockwaveSettings& settings = {});
  void Update(float dt);
  void Clear() { waves_ = {}; }

  // Every ripple of `settings`: the rings drawn now, the other slots off. Leaves the rest alone.
  void WriteTo(rendering::PostSettings& settings) const;

  int Count() const;  // the waves not over yet (delayed ones too)
  bool Active() const { return Count() > 0; }

 private:
  struct Wave {
    glm::vec2 center{0.0f};
    ShockwaveSettings settings;
    float age = 0.0f;
    bool live = false;
  };
  std::array<Wave, rendering::kMaxRipples> waves_{};
};

}  // namespace wlw::fx
