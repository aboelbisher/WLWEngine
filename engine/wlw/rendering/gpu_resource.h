#pragma once

#include <cstdint>
#include <functional>
#include <memory>

namespace wlw::rendering {

// The backend's half of a resource (a GL texture, program or framebuffer). The renderer makes it
// the first time it needs the resource, and it's freed with the resource.
class GpuResource {
 public:
  virtual ~GpuResource() = default;
};

// What games make GPU things from: textures, shaders, render targets. Like a Mesh, a resource is
// plain CPU-side data that is made without the GPU (no device, no GL context, usable in tests). The
// renderer makes its GPU side on first use, or before the first frame for everything alive then
// (RenderingDriver::WarmUp), the way Unity, Godot and Unreal hide their GPU device from game code.
//
// Every resource is on the alive list from its constructor to its destructor, however it's owned
// (Create functions return unique_ptr; the caller may turn it into a shared_ptr).
class Resource {
 public:
  virtual ~Resource();
  Resource(const Resource&) = delete;
  Resource& operator=(const Resource&) = delete;

  // Unique for the program's run, never reused: for sorting and batching without the GPU.
  uint32_t Id() const { return id_; }

  class InternalAccess;
  // Engine-only parts (the renderer's): see InternalAccess below.
  InternalAccess Internal() const;

 protected:
  Resource();

 private:
  uint32_t id_;
  uint32_t slot_ = 0;  // index in the alive list
  mutable std::unique_ptr<GpuResource> gpu_;

  friend struct AliveList;
};

// For the renderer, not games (x.Internal().Gpu()).
class Resource::InternalAccess {
 public:
  explicit InternalAccess(const Resource& resource) : resource_(resource) {}

  GpuResource* Gpu() const { return resource_.gpu_.get(); }
  void SetGpu(std::unique_ptr<GpuResource> gpu) const { resource_.gpu_ = std::move(gpu); }

  // Calls `visit` for every resource alive (the warm-up prepares them all before the first frame).
  // `visit` may make or destroy resources: made ones are visited too, destroyed ones skipped.
  static void ForEachAlive(const std::function<void(Resource&)>& visit);
  // How many resources are alive (tests).
  static size_t AliveCount();

 private:
  const Resource& resource_;
};

inline Resource::InternalAccess Resource::Internal() const { return InternalAccess(*this); }

}  // namespace wlw::rendering
