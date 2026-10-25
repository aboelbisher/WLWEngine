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
//   scene::AddBlur(App().Window().MainViewport(), {.scale = 0.25f});   // a blurred background behind a menu
//
// Bloom is not one of them: it is a setting (rendering/post_settings.h, PostSettings::bloom).
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
