#include "wlw/ui/font.h"

#include <algorithm>
#include <atomic>

#include "stb_truetype.h"
#include "wlw/core/log.h"
#include "wlw/platform/platform.h"

namespace wlw::ui {

struct Font::Impl {
  std::vector<uint8_t> ttf;  // stb_truetype reads the file in place: keep it alive
  stbtt_fontinfo info{};
  float scale = 1.0f;  // font units -> atlas pixels
};

std::shared_ptr<Font> Font::Load(const std::string& path, const FontOptions& options) {
  std::optional<std::vector<uint8_t>> data = platform::ReadFile(path);
  if (!data) {
    WLW_LOG(LogUI, Error, "Font not found: %s", path.c_str());
    return nullptr;
  }
  auto font = FromMemory(std::move(*data), options);
  if (!font) WLW_LOG(LogUI, Error, "Not a usable TrueType font: %s", path.c_str());
  return font;
}

std::shared_ptr<Font> Font::FromMemory(std::vector<uint8_t> ttf, const FontOptions& options) {
  std::shared_ptr<Font> font(new Font());
  font->impl_ = std::make_unique<Impl>();
  font->impl_->ttf = std::move(ttf);
  const unsigned char* data = font->impl_->ttf.data();
  if (font->impl_->ttf.empty() || !stbtt_InitFont(&font->impl_->info, data, stbtt_GetFontOffsetForIndex(data, 0))) {
    return nullptr;
  }
  if (!font->Bake(options)) return nullptr;
  static std::atomic<uint32_t> next_id{1};
  font->id_ = next_id++;
  return font;
}

Font::~Font() = default;

bool Font::Bake(const FontOptions& options) {
  bake_size_ = options.bake_size;
  const stbtt_fontinfo& info = impl_->info;
  impl_->scale = stbtt_ScaleForPixelHeight(&info, bake_size_);
  int ascent = 0, descent = 0, line_gap = 0;
  stbtt_GetFontVMetrics(&info, &ascent, &descent, &line_gap);
  ascent_ = ascent * impl_->scale;
  descent_ = -descent * impl_->scale;

  // stb's SDF: `on_edge` is the value stored on the outline (128 = 0.5); each pixel away from it
  // changes the value by `per_pixel`, so the field fades to 0/255 at `padding` pixels out.
  constexpr unsigned char kOnEdge = 128;
  const float per_pixel = static_cast<float>(kOnEdge) / static_cast<float>(options.padding);
  distance_per_pixel_ = per_pixel / 255.0f;

  // 1. Rasterise every glyph's distance field.
  struct Bitmap {
    std::vector<uint8_t> pixels;
    int width = 0, height = 0;
  };
  std::array<Bitmap, kLast - kFirst + 1> bitmaps;
  for (int c = kFirst; c <= kLast; ++c) {
    Glyph& glyph = glyphs_[c - kFirst];
    int advance = 0, left_bearing = 0;
    stbtt_GetCodepointHMetrics(&info, c, &advance, &left_bearing);
    glyph.advance = advance * impl_->scale;

    int w = 0, h = 0, xoff = 0, yoff = 0;
    unsigned char* sdf = stbtt_GetCodepointSDF(&info, impl_->scale, c, options.padding, kOnEdge, per_pixel, &w, &h,
                                               &xoff, &yoff);
    if (!sdf) continue;  // no outline (space)
    glyph.offset = {static_cast<float>(xoff), static_cast<float>(yoff)};  // y down, relative to baseline
    glyph.size = {static_cast<float>(w), static_cast<float>(h)};
    bitmaps[c - kFirst] = {std::vector<uint8_t>(sdf, sdf + w * h), w, h};
    stbtt_FreeSDF(sdf, nullptr);
  }

  // 2. Pack them into one atlas, row by row ("shelf packing"): simple, and good enough for one
  //    font's worth of similar-sized glyphs. 1 pixel of space between neighbours.
  constexpr int kWidth = 512;
  int x = 1, y = 1, row_height = 0;
  std::array<glm::ivec2, kLast - kFirst + 1> positions{};
  for (size_t i = 0; i < bitmaps.size(); ++i) {
    const Bitmap& b = bitmaps[i];
    if (b.width == 0) continue;
    if (x + b.width + 1 > kWidth) {  // row full: start the next shelf
      x = 1;
      y += row_height + 1;
      row_height = 0;
    }
    positions[i] = {x, y};
    x += b.width + 1;
    row_height = std::max(row_height, b.height);
  }
  atlas_.width = kWidth;
  atlas_.height = y + row_height + 1;
  atlas_.pixels.assign(static_cast<size_t>(atlas_.width) * atlas_.height, 0);
  for (size_t i = 0; i < bitmaps.size(); ++i) {
    const Bitmap& b = bitmaps[i];
    if (b.width == 0) continue;
    for (int row = 0; row < b.height; ++row) {
      std::copy_n(b.pixels.begin() + row * b.width, b.width,
                  atlas_.pixels.begin() + (positions[i].y + row) * atlas_.width + positions[i].x);
    }
    Glyph& glyph = glyphs_[i];
    glyph.uv_min = {static_cast<float>(positions[i].x) / atlas_.width, static_cast<float>(positions[i].y) / atlas_.height};
    glyph.uv_max = {static_cast<float>(positions[i].x + b.width) / atlas_.width,
                    static_cast<float>(positions[i].y + b.height) / atlas_.height};
  }
  return true;
}

const Font::Glyph& Font::GlyphFor(char c) const {
  const int code = static_cast<unsigned char>(c);
  return glyphs_[(code >= kFirst && code <= kLast ? code : '?') - kFirst];
}

float Font::Kerning(char a, char b) const {
  return stbtt_GetCodepointKernAdvance(&impl_->info, static_cast<unsigned char>(a), static_cast<unsigned char>(b)) *
         impl_->scale;
}

float Font::Measure(std::string_view text, float size) const {
  const float s = size / bake_size_;
  float width = 0.0f;
  for (size_t i = 0; i < text.size(); ++i) {
    if (i > 0) width += Kerning(text[i - 1], text[i]) * s;
    width += GlyphFor(text[i]).advance * s;
  }
  return width;
}

void Font::Layout(std::string_view text, float size, glm::vec2 pen, std::vector<GlyphQuad>& out) const {
  const float s = size / bake_size_;  // atlas pixels -> caller units
  for (size_t i = 0; i < text.size(); ++i) {
    if (i > 0) pen.x += Kerning(text[i - 1], text[i]) * s;
    const Glyph& glyph = GlyphFor(text[i]);
    if (glyph.size.x > 0.0f) {
      GlyphQuad quad;
      quad.min = pen + glyph.offset * s;
      quad.max = quad.min + glyph.size * s;
      quad.uv_min = glyph.uv_min;
      quad.uv_max = glyph.uv_max;
      out.push_back(quad);
    }
    pen.x += glyph.advance * s;
  }
}

float Font::Ascent(float size) const { return ascent_ * size / bake_size_; }
float Font::Descent(float size) const { return descent_ * size / bake_size_; }

float Font::EdgeForOutline(float width) const {
  // `width` of the size, in atlas pixels, times how much the field changes per pixel.
  return std::max(0.5f - width * bake_size_ * distance_per_pixel_, 0.02f);
}

}  // namespace wlw::ui
