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
class Resource {
 public:
  virtual ~Resource() = default;
  Resource(const Resource&) = delete;
  Resource& operator=(const Resource&) = delete;

  // Unique for the program's run, never reused: for sorting and batching without the GPU.
  uint32_t Id() const { return id_; }

  // ---- For the renderer ----
  GpuResource* Gpu() const { return gpu_.get(); }
  void SetGpu(std::unique_ptr<GpuResource> gpu) const { gpu_ = std::move(gpu); }

  // Calls `visit` for every resource that is alive (made with a Create function). The renderer's
  // warm-up uses it to prepare them all before the first frame.
  static void ForEachAlive(const std::function<void(Resource&)>& visit);

 protected:
  Resource();
  // Every Create function registers what it made, so ForEachAlive can find it.
  static void Register(const std::shared_ptr<Resource>& resource);

 private:
  uint32_t id_;
  mutable std::unique_ptr<GpuResource> gpu_;
};

}  // namespace wlw::rendering
