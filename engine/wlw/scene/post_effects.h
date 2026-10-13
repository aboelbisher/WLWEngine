#pragma once

#include <memory>
#include <vector>

#include "wlw/rendering/shader_material.h"
#include "wlw/rendering/texture.h"
#include "wlw/scene/viewport.h"

// Ready-made post effects: post passes (rendering/post_pass.h) with the engine's shaders
// (rendering/shaders/post/*). Each adds its passes after the viewport's existing ones and returns
// the materials that control it, to change while the game runs (SetParam).
//
//   scene::AddBloom(App().Window().GetScreen(), {.threshold = 0.7f});   // glow on the whole screen
namespace wlw::scene {

struct BlurSettings {
  float scale = 0.5f;   // the passes' size as a fraction of the viewport's: smaller = cheaper and wider
  float spread = 1.0f;  // tap spacing in texels: more = wider, eventually streaky
  int iterations = 1;   // each is one pass across + one down; more = smoother and wider
  rendering::TextureFormat format = rendering::TextureFormat::RGBA8;  // RGBA16F keeps values above 1
};

// The blur's shader materials (one per pass, all the same shader): SetParam("uSpread", s).
struct Blur {
  std::vector<std::shared_ptr<rendering::ShaderMaterial>> passes;
};

// A separable Gaussian blur (9 taps in 5 reads per direction). E.g. to melt soft dots into one
// shape before a threshold pass (metaballs), or a blurred background behind a menu.
Blur AddBlur(Viewport& viewport, const BlurSettings& settings = {});

struct BloomSettings {
  float threshold = 0.8f;  // brightness (the largest of r, g, b) where glow starts; above 1 needs an
                           // RGBA16F viewport (HDR), else 0.6-0.9 for an ordinary picture
  float knee = 0.2f;       // how gradually it starts below the threshold
  float strength = 0.8f;   // how much glow is added
  float scale = 0.25f;     // the glow's resolution: a quarter is cheap on phones and soft anyway
  float spread = 1.5f;
  int iterations = 2;      // blur rounds: more = a wider halo
  rendering::TextureFormat format = rendering::TextureFormat::RGBA8;  // of the glow passes
};

// The bloom's materials: bright.SetParam("uThreshold" / "uKnee", ...), combine.SetParam("uStrength", ...).
struct Bloom {
  std::shared_ptr<rendering::ShaderMaterial> bright;
  std::shared_ptr<rendering::ShaderMaterial> combine;
};

// Bloom (glow): the bright parts, blurred at low resolution and added back onto the picture. Adds
// 2 + 2 x iterations passes. Reads the picture as it is when added (after the passes before it).
Bloom AddBloom(Viewport& viewport, const BloomSettings& settings = {});

}  // namespace wlw::scene
