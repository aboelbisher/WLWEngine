#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

#include "wlw/rendering/gpu_resource.h"
#include "wlw/utils/image_loader.h"

namespace wlw::rendering {

// How a texture stores its texels. The 8-bit formats take bytes, the 16F ones floats (kept on the
// GPU as half floats: ~3 significant digits, enough for data like a height field, and filterable
// on every GLES 3.0 device, unlike 32-bit floats).
// RG16F: two signed values per texel (screen offsets: the distortion buffer, post_settings.h).
enum class TextureFormat { R8, RGB8, RGBA8, R16F, RGBA16F, RG16F };

// Bytes per texel of the data a game passes in (floats for the 16F formats).
size_t TexelBytes(TextureFormat format);

struct TextureSettings {
  TextureFormat format = TextureFormat::RGBA8;
  bool smooth = true;    // linear filtering between texels; false = nearest (pixel art, lookups)
  bool repeat = false;   // wrap around at the edges; false = clamp to the edge texels
  bool mipmaps = false;  // smaller copies for drawing it small (far away in 3D): images, not data
};

// A picture the GPU samples: a 2D texture or a cubemap.
class WTexture : public Resource {
 public:
  virtual bool IsCubemap() const = 0;
};

// A 2D texture: from an image, from the game's own data, or what a RenderTarget draws into.
// Made without the GPU; the renderer uploads it the first time it's drawn (or at warm-up).
class WTexture2D : public WTexture {
 public:
  // From the game's data (see TextureSettings): width * height texels, row by row from the top,
  // as bytes or floats (see TextureFormat). `pixels` null = all zero, filled later with Update.
  static std::unique_ptr<WTexture2D> Create(int width, int height, const TextureSettings& settings = {},
                                            const void* pixels = nullptr);
  // An image file's texture (utils::ImageLoader): repeating and mipmapped (it may be drawn small,
  // far away in 3D).
  static std::unique_ptr<WTexture2D> FromImage(const utils::RawImage& image);

  // Replaces every texel (same size and format, laid out as for Create). The data is copied, so
  // `pixels` may go away; the GPU gets it before the texture is next drawn. E.g. data that
  // changes each frame.
  void Update(const void* pixels);

  int Width() const { return width_; }
  int Height() const { return height_; }
  TextureFormat Format() const { return settings_.format; }
  const TextureSettings& Settings() const { return settings_; }
  bool IsCubemap() const override { return false; }

  class InternalAccess;
  InternalAccess Internal() const;

 private:
  WTexture2D(int width, int height, const TextureSettings& settings, bool render_target);  // use Create

  int width_ = 0;
  int height_ = 0;
  TextureSettings settings_;
  bool render_target_ = false;
  mutable std::vector<uint8_t> pixels_;
  uint64_t version_ = 0;

  friend class RenderTarget;
};

// For the renderer, not games.
class WTexture2D::InternalAccess : public Resource::InternalAccess {
 public:
  explicit InternalAccess(const WTexture2D& texture) : Resource::InternalAccess(texture), texture_(texture) {}

  // Texels waiting for the GPU (empty: none, or all zero for a new texture); Version() changes
  // with every Update. The renderer frees them once uploaded (ReleasePixels).
  const std::vector<uint8_t>& Pixels() const { return texture_.pixels_; }
  uint64_t Version() const { return texture_.version_; }
  void ReleasePixels() const { std::vector<uint8_t>().swap(texture_.pixels_); }
  // A render target's texture: the GPU draws into it, nothing is uploaded. Its size follows the
  // target's, and its format may fall back to RGBA8 (see RenderTarget).
  bool IsRenderTarget() const { return texture_.render_target_; }

 private:
  const WTexture2D& texture_;
};

inline WTexture2D::InternalAccess WTexture2D::Internal() const { return InternalAccess(*this); }

// Six square faces (+X, -X, +Y, -Y, +Z, -Z), for a skybox. Uploaded like a WTexture2D.
class WCubemap : public WTexture {
 public:
  static std::unique_ptr<WCubemap> Create(const std::array<utils::RawImage, 6>& faces);

  bool IsCubemap() const override { return true; }

  class InternalAccess;
  InternalAccess Internal() const;

 private:
  explicit WCubemap(const std::array<utils::RawImage, 6>& faces);  // use Create

  mutable std::vector<utils::RawImage> faces_;
};

// For the renderer, not games.
class WCubemap::InternalAccess : public Resource::InternalAccess {
 public:
  explicit InternalAccess(const WCubemap& cubemap) : Resource::InternalAccess(cubemap), cubemap_(cubemap) {}

  // The faces until they are on the GPU (then freed: ReleaseFaces).
  const std::vector<utils::RawImage>& Faces() const { return cubemap_.faces_; }
  void ReleaseFaces() const { std::vector<utils::RawImage>().swap(cubemap_.faces_); }

 private:
  const WCubemap& cubemap_;
};

inline WCubemap::InternalAccess WCubemap::Internal() const { return InternalAccess(*this); }

}  // namespace wlw::rendering
