#pragma once

#include <algorithm>
#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include <glm/glm.hpp>

namespace wlw::ui {

struct FontOptions {
  float bake_size = 48.0f;  // glyph height (ascent to descent) in atlas pixels when baking
  int padding = 6;          // atlas pixels of distance field around each glyph (limits outlines and glows)
};

// One glyph to draw: where (in the caller's units) and which part of the atlas.
struct GlyphQuad {
  glm::vec2 min{0.0f};
  glm::vec2 max{0.0f};
  glm::vec2 uv_min{0.0f};
  glm::vec2 uv_max{0.0f};
  int index = 0;  // its character's position in the string (spaces get no quad, so numbers can be skipped)
};

// A TrueType font baked into a signed-distance-field (SDF) atlas.
//
// A font file stores glyph *outlines* (curves); GPUs draw triangles and textures. So at load time
// every glyph is rasterised into one texture - but instead of coverage, each atlas pixel stores
// its distance to the glyph's edge (0.5 = on the edge, above = inside, below = outside). A shader
// then cuts the edge at 0.5 with smooth anti-aliasing, which stays sharp at any scale, and
// thicker cut-offs give outlines and shadows from the same data.
//
// Sizes are "line heights": the distance from the highest ascender to the lowest descender, in
// whatever units the caller uses (Canvas units for the UI). Covers printable ASCII (32..126);
// other characters draw as '?'. CPU only: the UI renderer uploads the atlas.
// Load / FromMemory return a unique_ptr. Held by a shared_ptr, a font is kept alive by the Canvas
// until the text is drawn (the caller may let go first); held by a unique_ptr, it must outlive the draw.
class Font : public std::enable_shared_from_this<Font> {
 public:
  struct Atlas {
    int width = 0;
    int height = 0;
    std::vector<uint8_t> pixels;  // one channel, row-major, top row first
  };

  // Reads the file with platform::ReadFile (works for APK assets). Null on failure (logged).
  static std::unique_ptr<Font> Load(const std::string& path, const FontOptions& options = {});
  static std::unique_ptr<Font> FromMemory(std::vector<uint8_t> ttf, const FontOptions& options = {});
  ~Font();

  float Measure(std::string_view text, float size) const;  // advance width of the whole string
  // Appends one quad per visible glyph; `pen` is the left end of the baseline.
  void Layout(std::string_view text, float size, glm::vec2 pen, std::vector<GlyphQuad>& out) const;
  float Ascent(float size) const;   // baseline -> top of the tallest glyphs (positive)
  float Descent(float size) const;  // baseline -> bottom of descenders (positive)

  // The SDF threshold that draws the glyph `width` (a fraction of the size) thicker: for
  // outlines. 0.5 = the normal edge. Limited by FontOptions::padding.
  float EdgeForOutline(float width) const;
  // How many atlas pixels the distance field takes to go from 0 to 1 (about 2 x padding): the UI
  // shader turns it into screen pixels to anti-alias the edge.
  float DistanceRange() const { return 1.0f / distance_per_pixel_; }
  // Atlas pixels of field around each glyph (FontOptions::padding): a GlyphQuad includes them.
  float Padding() const { return padding_; }
  // How far past the glyph's edge, in atlas pixels, a cut at `edge` (EdgeForOutline's units) reaches.
  float Reach(float edge) const { return std::max(0.5f - edge, 0.0f) / distance_per_pixel_; }
  float BakeSize() const { return bake_size_; }  // atlas pixels per unit of size

  const Atlas& GetAtlas() const { return atlas_; }
  uint32_t Id() const { return id_; }  // unique per font: the renderer caches its texture by it

 private:
  struct Glyph {
    float advance = 0.0f;    // pen movement, atlas pixels
    glm::vec2 offset{0.0f};  // top-left of the bitmap relative to the pen on the baseline
    glm::vec2 size{0.0f};    // bitmap size, atlas pixels (0 for spaces)
    glm::vec2 uv_min{0.0f};
    glm::vec2 uv_max{0.0f};
  };
  static constexpr int kFirst = 32;
  static constexpr int kLast = 126;

  Font() = default;
  bool Bake(const FontOptions& options);
  const Glyph& GlyphFor(char c) const;
  float Kerning(char a, char b) const;  // atlas pixels

  struct Impl;  // stb_truetype state (kept out of this header)
  std::unique_ptr<Impl> impl_;
  std::array<Glyph, kLast - kFirst + 1> glyphs_{};
  Atlas atlas_;
  float bake_size_ = 48.0f;
  float padding_ = 6.0f;             // atlas pixels
  float ascent_ = 0.0f;              // atlas pixels
  float descent_ = 0.0f;             // atlas pixels, positive
  float distance_per_pixel_ = 0.0f;  // SDF value change per atlas pixel (0..1 scale)
  uint32_t id_ = 0;
};

}  // namespace wlw::ui
