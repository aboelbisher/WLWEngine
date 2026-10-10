#pragma once
#include <memory>

namespace wlw::rendering {

class WTexture {
public:
    virtual ~WTexture() = default;
    virtual void Bind(unsigned int slot = 0) const = 0;
    virtual void Unbind() const = 0;
    
    unsigned int GetID() const { return gpu_id_; }

protected:
    unsigned int gpu_id_ = 0;
};

// How a texture stores its texels. The 8-bit formats take bytes, the 16F ones floats (kept on the
// GPU as half floats: ~3 significant digits, enough for data like a height field, and filterable
// on every GLES 3.0 device, unlike 32-bit floats).
enum class TextureFormat { R8, RGB8, RGBA8, R16F, RGBA16F };

// For textures a game makes from its own data (RenderDevice::CreateTexture2D(width, height, ...)).
struct TextureSettings {
    TextureFormat format = TextureFormat::RGBA8;
    bool smooth = true;   // linear filtering between texels; false = nearest (pixel art, lookups)
    bool repeat = false;  // wrap around at the edges; false = clamp to the edge texels
};

class WTexture2D : public WTexture {
public:
    virtual void Bind(unsigned int slot = 0) const override = 0;
    virtual void Unbind() const override = 0;
    // Replaces every texel (same size and format): width * height texels, row by row from the
    // top, as bytes or floats (see TextureFormat). E.g. data that changes each frame.
    virtual void Update(const void* pixels) = 0;

    int Width() const { return width_; }
    int Height() const { return height_; }
    TextureFormat Format() const { return format_; }

protected:
    int width_ = 0;
    int height_ = 0;
    TextureFormat format_ = TextureFormat::RGBA8;
};

class WCubemap : public WTexture {
public:
    virtual void Bind(unsigned int slot = 0) const override = 0;
    virtual void Unbind() const override = 0;
};

} // namespace wlw::rendering
