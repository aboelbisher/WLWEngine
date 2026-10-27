#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <memory>
#include <vector>

#include "wlw/core/math.h"
#include "wlw/rendering/texture.h"

// Procedural textures, like primitives.h makes procedural meshes: pixels computed on the CPU
// (testable without a GPU), then one texture made from them, once.
namespace wlw::utils {

// A round dot as RGBA8 pixels, size x size, row by row: white, alpha fading from 1 at the centre
// to 0 at the edge of the inscribed circle as (1 - d²)² (d = distance from the centre / radius):
// smooth everywhere, no hard rim. Each texel is sampled at its centre.
inline std::vector<uint8_t> RadialFalloffPixels(int size) {
  size = std::max(size, 1);
  std::vector<uint8_t> pixels(static_cast<size_t>(size) * size * 4);
  for (int y = 0; y < size; ++y) {
    for (int x = 0; x < size; ++x) {
      const float u = (static_cast<float>(x) + 0.5f) / static_cast<float>(size) * 2.0f - 1.0f;
      const float v = (static_cast<float>(y) + 0.5f) / static_cast<float>(size) * 2.0f - 1.0f;
      const float f = std::max(1.0f - (u * u + v * v), 0.0f);
      uint8_t* texel = &pixels[(static_cast<size_t>(y) * size + x) * 4];
      texel[0] = texel[1] = texel[2] = 255;
      texel[3] = static_cast<uint8_t>(std::lround(f * f * 255.0f));
    }
  }
  return pixels;
}

// The dot above as a texture: glows, light spots, soft shadows, particles. Drawn Additive (e.g.
// Canvas::Image with a tint), overlapping dots add up into a smooth density field: threshold it
// and you get metaballs (a liquid surface). Smooth sampling, clamped at the edges. Make it once.
inline std::unique_ptr<rendering::WTexture2D> RadialFalloffTexture(int size = 64) {
  const std::vector<uint8_t> pixels = RadialFalloffPixels(size);
  return rendering::WTexture2D::Create(std::max(size, 1), std::max(size, 1),
                                       {rendering::TextureFormat::RGBA8, /*smooth=*/true, /*repeat=*/false},
                                       pixels.data());
}

// A white shape from per-pixel alpha `alpha(u, v)` (u, v in -1..1 at texel centres, v down) as RGBA8 pixels.
template <typename AlphaFn>
inline std::vector<uint8_t> AlphaShapePixels(int size, AlphaFn alpha) {
  size = std::max(size, 1);
  std::vector<uint8_t> pixels(static_cast<size_t>(size) * size * 4);
  for (int y = 0; y < size; ++y) {
    for (int x = 0; x < size; ++x) {
      const float u = (static_cast<float>(x) + 0.5f) / static_cast<float>(size) * 2.0f - 1.0f;
      const float v = (static_cast<float>(y) + 0.5f) / static_cast<float>(size) * 2.0f - 1.0f;
      uint8_t* texel = &pixels[(static_cast<size_t>(y) * size + x) * 4];
      texel[0] = texel[1] = texel[2] = 255;
      texel[3] = static_cast<uint8_t>(std::lround(std::clamp(alpha(u, v), 0.0f, 1.0f) * 255.0f));
    }
  }
  return pixels;
}

// A star of light: a small soft core and `points` thin rays (the first along +u, i.e. to the right), each
// `ray_width` wide at the centre (in radii) and tapering to nothing at the edge, brightness falling as (1 - r)².
// The glint on a gem, a spark's flare. Smooth everywhere; 0 at and beyond the inscribed circle.
inline std::vector<uint8_t> StarPixels(int size, int points = 4, float ray_width = 0.12f) {
  points = std::max(points, 1);
  const float step = kTau / static_cast<float>(points);
  return AlphaShapePixels(size, [&](float u, float v) {
    const float r = std::sqrt(u * u + v * v);
    const float core_d = std::min(r / 0.35f, 1.0f);
    float alpha = (1.0f - core_d * core_d) * (1.0f - core_d * core_d);
    for (int k = 0; k < points; ++k) {
      const float angle = step * static_cast<float>(k);
      const float along = u * std::cos(angle) - v * std::sin(angle);  // v points down: rays turn counter-clockwise
      if (along <= 0.0f || along >= 1.0f) continue;
      const float across = std::abs(u * std::sin(angle) + v * std::cos(angle));
      const float width = ray_width * (1.0f - along);  // tapers to a point at the rim
      if (across >= width) continue;
      const float fade = (1.0f - along) * (1.0f - along);
      const float edge = 1.0f - across / width;
      alpha = std::max(alpha, fade * edge * edge);
    }
    return alpha;
  });
}

// A soft ring: alpha peaks at `radius` (in radii of the inscribed circle) and falls to 0 at `width` either side as
// (1 - x²)² (x = distance from the ring line / width). Shockwaves, halos, a pulse around a group.
inline std::vector<uint8_t> RingPixels(int size, float radius = 0.7f, float width = 0.2f) {
  width = std::max(width, 1e-3f);
  return AlphaShapePixels(size, [&](float u, float v) {
    const float x = (std::sqrt(u * u + v * v) - radius) / width;
    const float f = std::max(1.0f - x * x, 0.0f);
    return f * f;
  });
}

// The two above as textures (smooth sampling, clamped): shapes for BillboardBatch / SpriteBatch materials and
// Canvas::Image. Make each once.
inline std::unique_ptr<rendering::WTexture2D> ShapeTexture(int size, const std::vector<uint8_t>& pixels) {
  return rendering::WTexture2D::Create(std::max(size, 1), std::max(size, 1),
                                       {rendering::TextureFormat::RGBA8, /*smooth=*/true, /*repeat=*/false},
                                       pixels.data());
}
inline std::unique_ptr<rendering::WTexture2D> StarTexture(int size = 64, int points = 4, float ray_width = 0.12f) {
  return ShapeTexture(size, StarPixels(size, points, ray_width));
}
inline std::unique_ptr<rendering::WTexture2D> RingTexture(int size = 64, float radius = 0.7f, float width = 0.2f) {
  return ShapeTexture(size, RingPixels(size, radius, width));
}

// A shockwave ring for a distortion material (rendering::Material::distortion, post_settings.h' DistortionSettings):
// a direction map like a normal map's. Red and green hold the push's direction, outwards from the centre (x right, y
// UP in the image, -1..1 stored as 0..1); alpha its size, peaking at `radius` and falling to 0 at `width` either side
// as (1 - x²)², the profile of rendering::Ripple. On a BillboardBatch the push is alpha x the quad's half size x
// its colour's alpha x intensity: grow the quad and the ring grows, with a push in proportion (a negative intensity
// pulls inwards).
inline std::vector<uint8_t> RingDistortionPixels(int size, float radius = 0.7f, float width = 0.2f) {
  size = std::max(size, 1);
  width = std::max(width, 1e-3f);
  std::vector<uint8_t> pixels(static_cast<size_t>(size) * size * 4);
  auto unit = [](float value) {
    return static_cast<uint8_t>(std::lround(std::clamp(value * 0.5f + 0.5f, 0.0f, 1.0f) * 255.0f));
  };
  for (int y = 0; y < size; ++y) {
    for (int x = 0; x < size; ++x) {
      const float u = (static_cast<float>(x) + 0.5f) / static_cast<float>(size) * 2.0f - 1.0f;
      const float v = (static_cast<float>(y) + 0.5f) / static_cast<float>(size) * 2.0f - 1.0f;  // down
      const float r = std::sqrt(u * u + v * v);
      const float across = (r - radius) / width;
      const float f = std::max(1.0f - across * across, 0.0f);
      uint8_t* texel = &pixels[(static_cast<size_t>(y) * size + x) * 4];
      texel[0] = unit(r > 0.0f ? u / r : 0.0f);
      texel[1] = unit(r > 0.0f ? -v / r : 0.0f);
      texel[2] = 128;
      texel[3] = static_cast<uint8_t>(std::lround(f * f * 255.0f));
    }
  }
  return pixels;
}
inline std::unique_ptr<rendering::WTexture2D> RingDistortionTexture(int size = 128, float radius = 0.7f,
                                                                    float width = 0.2f) {
  return ShapeTexture(size, RingDistortionPixels(size, radius, width));
}

}  // namespace wlw::utils
