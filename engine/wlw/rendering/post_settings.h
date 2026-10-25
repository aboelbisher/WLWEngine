#pragma once

// A viewport's post-processing settings: plain data the renderer reads every frame
// (Viewport::Post().Settings()), like Godot's Environment, Unity's Volume profile or Unreal's post
// process settings. The effects run in a fixed order, whatever order the game sets them in:
//
//   3D -> Picture canvas layers -> the game's post passes -> bloom -> output pass -> Overlay canvas layers
//
// The OUTPUT PASS is the frame's last full-screen pass (Unity URP's UberPost, Unreal's Tonemapper,
// Godot's tonemap pass): it reads the HDR scene picture once and writes the destination, doing
// exposure, tone mapping and dithering in one go. It only runs when a setting needs it
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

struct PostSettings {
  ToneMapSettings tone_map;
  BloomSettings bloom;

  // Whether the output pass runs: off when every setting is at its "do nothing" value.
  bool NeedsOutputPass() const {
    return tone_map.mapper != ToneMapper::Clamp || tone_map.exposure != 1.0f || tone_map.dither || bloom.enabled;
  }
};

}  // namespace wlw::rendering
