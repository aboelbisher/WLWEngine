#pragma once

// A viewport's post-processing settings: plain data the renderer reads every frame
// (Viewport::Post().Settings()), like Godot's Environment, Unity's Volume profile or Unreal's post
// process settings. The effects run in a fixed order, whatever order the game sets them in:
//
//   3D -> Picture canvas layers -> the game's post passes -> output pass -> Overlay canvas layers
//
// The OUTPUT PASS is the frame's last full-screen pass (Unity URP's UberPost, Unreal's Tonemapper,
// Godot's tonemap pass): it reads the HDR scene picture once and writes the destination, doing
// exposure, tone mapping and dithering in one go. It only runs when a setting needs it
// (PostSettings::NeedsOutputPass); otherwise the picture reaches the screen as before.
//
//   screen.SetFormat(rendering::TextureFormat::RGBA16F);       // values above 1 survive until here
//   screen.Post().Settings().tone_map = {.mapper = rendering::ToneMapper::Shoulder, .dither = true};
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

struct PostSettings {
  ToneMapSettings tone_map;

  // Whether the output pass runs: off when every setting is at its "do nothing" value.
  bool NeedsOutputPass() const {
    return tone_map.mapper != ToneMapper::Clamp || tone_map.exposure != 1.0f || tone_map.dither;
  }
};

}  // namespace wlw::rendering
