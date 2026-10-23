#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <memory>
#include <vector>

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

}  // namespace wlw::utils
