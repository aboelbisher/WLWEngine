#include "wlw/rendering/texture.h"

#include <algorithm>
#include <cstring>
#include <vector>

namespace wlw::rendering {

namespace {

// Every resource made by a Create function. Weak: the list never keeps one alive. Expired entries
// are dropped as ForEachAlive walks it.
std::vector<std::weak_ptr<Resource>>& AliveResources() {
  static std::vector<std::weak_ptr<Resource>> resources;
  return resources;
}

uint32_t NextResourceId() {
  static uint32_t next = 1;
  return next++;
}

}  // namespace

Resource::Resource() : id_(NextResourceId()) {}

void Resource::Register(const std::shared_ptr<Resource>& resource) { AliveResources().push_back(resource); }

void Resource::ForEachAlive(const std::function<void(Resource&)>& visit) {
  auto& resources = AliveResources();
  std::erase_if(resources, [](const std::weak_ptr<Resource>& entry) { return entry.expired(); });
  // A copy: `visit` may make resources (Register would invalidate the iteration).
  const std::vector<std::weak_ptr<Resource>> alive = resources;
  for (const auto& entry : alive) {
    if (auto resource = entry.lock()) visit(*resource);
  }
}

size_t TexelBytes(TextureFormat format) {
  switch (format) {
    case TextureFormat::R8: return 1;
    case TextureFormat::RGB8: return 3;
    case TextureFormat::RGBA8: return 4;
    case TextureFormat::R16F: return 4;      // one float
    case TextureFormat::RGBA16F: return 16;  // four floats
  }
  return 4;
}

WTexture2D::WTexture2D(int width, int height, const TextureSettings& settings, bool render_target)
    : width_(std::max(width, 1)), height_(std::max(height, 1)), settings_(settings), render_target_(render_target) {}

std::shared_ptr<WTexture2D> WTexture2D::Create(int width, int height, const TextureSettings& settings,
                                               const void* pixels) {
  auto texture = std::make_shared<WTexture2D>(width, height, settings, /*render_target=*/false);
  if (pixels) texture->Update(pixels);
  Register(texture);
  return texture;
}

std::shared_ptr<WTexture2D> WTexture2D::FromImage(const utils::RawImage& image) {
  const TextureFormat format = image.channels == 4   ? TextureFormat::RGBA8
                               : image.channels == 1 ? TextureFormat::R8
                                                     : TextureFormat::RGB8;
  return Create(static_cast<int>(image.size.x), static_cast<int>(image.size.y),
                {format, /*smooth=*/true, /*repeat=*/true, /*mipmaps=*/true}, image.pixels.data());
}

void WTexture2D::Update(const void* pixels) {
  if (render_target_ || pixels == nullptr) return;  // a render target's texels come from the GPU
  const size_t bytes = static_cast<size_t>(width_) * height_ * TexelBytes(settings_.format);
  pixels_.resize(bytes);
  std::memcpy(pixels_.data(), pixels, bytes);
  ++version_;
}

WCubemap::WCubemap(const std::array<utils::RawImage, 6>& faces) : faces_(faces.begin(), faces.end()) {}

std::shared_ptr<WCubemap> WCubemap::Create(const std::array<utils::RawImage, 6>& faces) {
  auto cubemap = std::make_shared<WCubemap>(faces);
  Register(cubemap);
  return cubemap;
}

void WCubemap::ReleaseFaces() const { std::vector<utils::RawImage>().swap(faces_); }

}  // namespace wlw::rendering
