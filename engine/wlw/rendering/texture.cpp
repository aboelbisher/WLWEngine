#include "wlw/rendering/texture.h"

#include <algorithm>
#include <cstring>
#include <vector>

namespace wlw::rendering {

namespace {

uint32_t NextResourceId() {
  static uint32_t next = 1;
  return next++;
}

}  // namespace

// Every resource alive, by raw pointer: a resource adds itself in its constructor and leaves in its
// destructor (swap-remove), so no owner type is needed. While ForEachAlive walks the list a destroyed
// resource only nulls its slot (indices must not move under the walk); the walk compacts afterwards.
struct AliveList {
  static std::vector<Resource*>& Slots() {
    static std::vector<Resource*> slots;
    return slots;
  }
  static int& Walking() {
    static int depth = 0;
    return depth;
  }
  static size_t& Holes() {
    static size_t holes = 0;
    return holes;
  }

  static void Add(Resource* resource) {
    auto& slots = Slots();
    resource->slot_ = static_cast<uint32_t>(slots.size());
    slots.push_back(resource);
  }
  static void Remove(Resource* resource) {
    auto& slots = Slots();
    if (Walking() > 0) {
      slots[resource->slot_] = nullptr;
      ++Holes();
      return;
    }
    Resource* last = slots.back();
    slots[resource->slot_] = last;
    last->slot_ = resource->slot_;
    slots.pop_back();
  }
  static void Compact() {
    auto& slots = Slots();
    std::erase(slots, nullptr);
    for (size_t i = 0; i < slots.size(); ++i) slots[i]->slot_ = static_cast<uint32_t>(i);
    Holes() = 0;
  }
};

Resource::Resource() : id_(NextResourceId()) { AliveList::Add(this); }

Resource::~Resource() { AliveList::Remove(this); }

void Resource::InternalAccess::ForEachAlive(const std::function<void(Resource&)>& visit) {
  auto& slots = AliveList::Slots();
  ++AliveList::Walking();
  // By index, re-reading the size: `visit` may add resources (appended, visited too) or destroy some
  // (their slots become null).
  for (size_t i = 0; i < slots.size(); ++i) {
    if (Resource* resource = slots[i]) visit(*resource);
  }
  if (--AliveList::Walking() == 0 && AliveList::Holes() > 0) AliveList::Compact();
}

size_t Resource::InternalAccess::AliveCount() { return AliveList::Slots().size() - AliveList::Holes(); }

size_t TexelBytes(TextureFormat format) {
  switch (format) {
    case TextureFormat::R8: return 1;
    case TextureFormat::RGB8: return 3;
    case TextureFormat::RGBA8: return 4;
    case TextureFormat::R16F: return 4;      // one float
    case TextureFormat::RGBA16F: return 16;  // four floats
    case TextureFormat::RG16F: return 8;     // two floats
  }
  return 4;
}

WTexture2D::WTexture2D(int width, int height, const TextureSettings& settings, bool render_target)
    : width_(std::max(width, 1)), height_(std::max(height, 1)), settings_(settings), render_target_(render_target) {}

std::unique_ptr<WTexture2D> WTexture2D::Create(int width, int height, const TextureSettings& settings,
                                               const void* pixels) {
  std::unique_ptr<WTexture2D> texture(new WTexture2D(width, height, settings, /*render_target=*/false));
  if (pixels) texture->Update(pixels);
  return texture;
}

std::unique_ptr<WTexture2D> WTexture2D::FromImage(const utils::RawImage& image) {
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

std::unique_ptr<WCubemap> WCubemap::Create(const std::array<utils::RawImage, 6>& faces) {
  return std::unique_ptr<WCubemap>(new WCubemap(faces));
}

}  // namespace wlw::rendering
