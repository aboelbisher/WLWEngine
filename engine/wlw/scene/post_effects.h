#pragma once

#include <memory>
#include <optional>
#include <vector>

#include "wlw/rendering/shader_material.h"
#include "wlw/rendering/texture.h"
#include "wlw/scene/viewport.h"

// Ready-made post effects: post passes (rendering/post_pass.h) with the engine's shaders
// (rendering/shaders/post/*). Each adds its passes after the viewport's existing ones and returns
// the materials that control it, to change while the game runs (SetParam).
//
//   scene::AddBloom(App().Window().MainViewport(), {.threshold = 0.7f});   // glow on the whole screen
namespace wlw::scene {

struct BlurSettings {
  float scale = 0.5f;   // the passes' size as a fraction of the viewport's: smaller = cheaper and wider
  float spread = 1.0f;  // tap spacing in texels: more = wider, eventually streaky
  int iterations = 1;   // each is one pass across + one down; more = smoother and wider
  std::optional<rendering::TextureFormat> format;  // nullopt = the scene picture's; RGBA16F keeps values above 1
};

// The blur's shader materials (one per pass, all the same shader): SetParam("uSpread", s).
struct Blur {
  std::vector<std::shared_ptr<rendering::ShaderMaterial>> passes;

  // Its resolution while the game runs (a quality level), on the viewport it was added to.
  void SetScale(Viewport& viewport, float scale) const;
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
  int iterations = 2;                              // blur rounds: more = a wider halo
  std::optional<rendering::TextureFormat> format;  // of the glow passes; nullopt = the scene picture's
};

// The bloom's materials: bright.SetParam("uThreshold" / "uKnee", ...), combine.SetParam("uStrength", ...).
struct Bloom {
  std::shared_ptr<rendering::ShaderMaterial> bright;
  std::shared_ptr<rendering::ShaderMaterial> combine;
  Blur blur;  // the glow's blur passes

  // The glow's resolution while the game runs (a quality level), on the viewport it was added to.
  void SetScale(Viewport& viewport, float scale) const;
};

// Bloom (glow): the bright parts, blurred at low resolution and added back onto the picture. Adds
// 2 + 2 x iterations passes. Reads the picture as it is when added (after the passes before it).
Bloom AddBloom(Viewport& viewport, const BloomSettings& settings = {});

struct ThresholdSettings {
  float threshold = 0.5f;  // density where the surface is
  float softness = 0.04f;  // edge fade (anti-aliasing): wider = softer edges
};

// Density -> solid shapes (post/threshold.glsl): where the picture's alpha passes the threshold it
// is solid, coloured by the average colour there; elsewhere see-through. After a blur, soft
// additive dots become liquid (metaballs). Usually on an offscreen viewport cleared to transparent,
// whose texture is then laid over the game:
//
//   std::shared_ptr<scene::Viewport> liquid = scene::Viewport::Create({.format = TextureFormat::RGBA16F, .depth =
//   false}); liquid->SetSizeFromScreen(0.5f);              // half resolution: 4x fewer pixels to fill
//   liquid->GetScene()->Root().AddChild(dots);   // a SpriteBatch, Additive
//   scene::AddBlur(*liquid, {.scale = 1.0f, .format = TextureFormat::RGBA16F});  // RGBA16F all the way:
//   scene::AddThreshold(*liquid, {.threshold = 0.45f});                          // RGBA8 clips colour x density
//                                                                                // at 1 and the middles go white
//
// Returns its material: SetParam("uThreshold" / "uSoftness", ...).
std::shared_ptr<rendering::ShaderMaterial> AddThreshold(Viewport& viewport, const ThresholdSettings& settings = {});

}  // namespace wlw::scene
