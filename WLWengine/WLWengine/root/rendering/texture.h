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

class WTexture2D : public WTexture {
public:
    virtual void Bind(unsigned int slot = 0) const override = 0;
    virtual void Unbind() const override = 0;
};

class WCubemap : public WTexture {
public:
    virtual void Bind(unsigned int slot = 0) const override = 0;
    virtual void Unbind() const override = 0;
};

} // namespace wlw::rendering
