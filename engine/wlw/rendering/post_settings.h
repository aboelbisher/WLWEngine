#pragma once

#include <algorithm>
#include <array>

#include <glm/glm.hpp>

#include "wlw/core/color.h"

// A viewport's post-processing settings: plain data the renderer reads every frame
// (Viewport::Post().Settings()), like Godot's Environment, Unity's Volume profile or Unreal's post
// process settings. The effects run in a fixed order, whatever order the game sets them in:
//
//   3D -> Picture canvas layers -> the game's post passes -> bloom -> output pass -> Overlay canvas layers
//
// The OUTPUT PASS is the frame's last full-screen pass (Unity URP's UberPost, Unreal's Tonemapper,
// Godot's tonemap pass): it reads the HDR scene picture once and writes the destination, doing
// distortion (ripples, chromatic aberration), exposure, vignette, tone mapping and dithering in one
// go. On mobile a full-screen pass costs mostly its read and write of every pixel, so effects folded
// in here cost a few instructions instead of a pass each. It only runs when a setting needs it
// (PostSettings::NeedsOutputPass); otherwise the picture reaches the screen as before.
//
//   screen.SetFormat(rendering::TextureFormat::RGBA16F);       // values above 1 survive until here
//   screen.Post().Settings().tone_map = {.mapper = rendering::ToneMapper::Shoulder, .dither = true};
//   screen.Post().Settings().bloom = {.enabled = true, .threshold = 1.2f};  // glow, added in the output pass
//
// Colours are still shaded gamma-encoded (linear lighting comes with roadmap 13.1), so the tone
// mappers here are the ones that make sense for such values; ACES / AgX wait for 13.1.
namespace wlw::rendering {

enum class ToneMapper {
  Clamp,     // values above 1 become 1, each channel on its own: today's picture (bright colours clip
             // to white or shift hue, e.g. (2, 1, 0.5) shows as (1, 1, 0.5))
  Shoulder,  // below 0.8 nothing changes; above, the brightest channel rolls off smoothly towards 1
             // and the others keep their ratios to it (the hue stays), fading to white only when
             // much brighter: Khronos PBR Neutral's shoulder, without its toe (which would darken
             // every colour a little in gamma space)
};

struct ToneMapSettings {
  ToneMapper mapper = ToneMapper::Clamp;
  float exposure = 1.0f;  // the picture is multiplied by it before the mapper
  bool dither = false;    // +-half a step of noise before the 8-bit screen: no banding in soft
                          // gradients (halos, backdrops); only useful from an RGBA16F picture
};

// Bloom (glow): what is brighter than the threshold spreads light around it. A progressive chain
// (Jimenez, "Next Generation Post Processing in Call of Duty: Advanced Warfare"; Unity URP's and
// Unreal's bloom work the same way): the picture is halved level by level (a 13-tap filter each,
// so a 1-pixel spark is never missed or flickers when it moves), then built back up, each level
// adding a blurred copy of the smaller one (a tent filter). The small levels give the wide soft
// tail, the big ones the bright core. The result is added in the output pass, before exposure and
// tone mapping, so a glow never clips. Values above 1 only exist in an RGBA16F picture: with
// one, a threshold above 1 makes only what is meant to glow (emissive things, sparks) glow.
struct BloomSettings {
  bool enabled = false;
  float threshold = 1.0f;  // brightness (the largest of r, g, b) where glow starts
  float knee = 0.5f;       // how far below the threshold it fades in (0 = a hard cut)
  float intensity = 0.5f;  // how much of the glow is added
  float scatter = 0.7f;    // 0..1: how much the small (wide) levels weigh against the big ones
                           // (Unity's name): more = a wider, softer halo
  float scale = 0.5f;      // the first level's size as a fraction of the viewport's (0.25 = cheaper)
  int max_levels = 6;      // the chain also stops when a level's short side would be under 16 px
};

// A ripple (a shockwave's ring): the picture under a ring around `center` is pushed outwards, as if
// seen through a moving lens of air (the distortion of Unreal's refraction, of most action games'
// shockwaves). Nothing in the scene moves: the output pass reads each pixel from a little further in,
// the most at the ring's middle, fading to nothing at its edges. Animate radius and strength
// (fx::Shockwaves grows and fades rings). Distances are fractions of the viewport's height, so rings
// stay round on any screen.
struct Ripple {
  glm::vec2 center{0.5f, 0.5f};  // in the viewport, 0..1 from its top-left corner (a screen point / its size)
  float radius = 0.0f;           // of the ring's middle
  float width = 0.1f;            // the ring's thickness (the push fades out within width / 2 either side)
  float strength = 0.0f;         // the largest push, outwards (negative: inwards); 0 = off
  float chroma = 0.0f;           // colour split at the ring: red is pushed (1 + chroma) as far, blue (1 - chroma)

  bool Active() const { return strength != 0.0f && width > 0.0f; }
};
inline constexpr int kMaxRipples = 4;

// Colour fringes towards the edges, as a cheap lens gives (Unity's Chromatic Aberration, Unreal's scene
// fringe): red and blue are read a little apart from green, outwards and inwards from the middle. The
// split grows with the square of the distance from the middle, so the middle stays sharp.
struct ChromaticAberrationSettings {
  float intensity = 0.0f;  // the split at the viewport's corners, as a fraction of its height (0.01 is
                           // strong); 0 = off
};

// Darkening towards the edges (Unity's and Unreal's vignette): frames the middle, draws the eye in.
// It follows the viewport's shape (an ellipse through the corners for a centred one).
struct VignetteSettings {
  float intensity = 0.0f;        // how far the corners go towards `color`, 0..1 (0 = off)
  float smoothness = 0.5f;       // where it starts: 0 = only at the corners, 1 = from the centre on
  glm::vec2 center{0.5f, 0.5f};  // 0..1 from the top-left corner
  Color color{0.0f, 0.0f, 0.0f, 1.0f};
};

struct PostSettings {
  ToneMapSettings tone_map;
  BloomSettings bloom;
  std::array<Ripple, kMaxRipples> ripples{};  // inactive ones cost nothing
  ChromaticAberrationSettings chromatic_aberration;
  VignetteSettings vignette;

  // Whether the picture is read from moved places (ripples or chromatic aberration).
  bool Distorts() const {
    return chromatic_aberration.intensity != 0.0f ||
           std::any_of(ripples.begin(), ripples.end(), [](const Ripple& ripple) { return ripple.Active(); });
  }
  // Whether the output pass runs: off when every setting is at its "do nothing" value.
  bool NeedsOutputPass() const {
    return tone_map.mapper != ToneMapper::Clamp || tone_map.exposure != 1.0f || tone_map.dither || bloom.enabled ||
           vignette.intensity > 0.0f || Distorts();
  }
};

}  // namespace wlw::rendering
