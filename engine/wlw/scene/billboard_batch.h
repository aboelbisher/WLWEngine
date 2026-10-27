#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

#include "wlw/core/geometry.h"
#include "wlw/rendering/instance_buffer.h"
#include "wlw/rendering/material.h"
#include "wlw/scene/node.h"

namespace wlw::scene {

// Camera-facing quads anywhere in 3D, in ONE draw call: sparks, streaks, flares, motes of light. The renderer half
// of a particle effect (fx::ParticleSystem::WriteTo fills one), like Unity's Particle System Renderer in Billboard /
// Stretched Billboard mode, Godot's billboard particles or a Niagara sprite renderer: the simulation decides where
// and what colour, this decides how it reaches the screen.
//
// Each quad is a 40-byte BillboardInstance (centre, size, stretch, rotation, RGBA8 colour, HDR intensity) and the
// GPU builds its 4 corners in the vertex shader (shaders/billboard.vert), in view space: a round quad faces the
// camera and turns by its rotation; a stretched one lies along its stretch vector's direction on screen, longer by
// that vector's on-screen length (velocity-aligned sparks). Not lit, no shadows. SpriteBatch is the flat 16-byte
// cousin for 2D dots on a plane (liquid, glow fields).
//
//   auto sparks = std::make_shared<scene::BillboardBatch>(512);
//   sparks->GetMaterial()->SetTexture(utils::StarTexture());   // the quad's shape (none: a soft round dot)
//   scene.Root().AddChild(sparks);
//   // every frame (or let fx::ParticleSystem::WriteTo do it):
//   std::span<rendering::BillboardInstance> out = sparks->Write(count);
//   ... fill out[i] ...
//   sparks->FitBounds();   // for culling
//
// Its material decides blending and the shape: Additive by default (light: order-independent, so the quads are
// never sorted, and an HDR picture lets them add past white into bloom); Alpha draws them in the order written (no
// per-quad sorting, unlike Unity's sort modes: use it for sparse, soft quads). The material's texture shapes each
// quad (alpha x rgb, uv (0, 0) at the quad's top-left on screen); without one the shader computes the soft (1-d²)²
// dot of utils::RadialFalloffTexture. A ShaderMaterial's shader isn't used (only its blend mode). Node scale: its
// length along x scales sizes (a uniform scale is assumed for them); positions and stretches take the full transform.
class BillboardBatch : public Node3D {
 public:
  // `capacity`: the most quads it holds; reserved now, never grown (Write cuts at it).
  explicit BillboardBatch(size_t capacity = 1024) : Node3D(NodeKind::BillboardBatch), capacity_(capacity) {
    billboards_.reserve(capacity);
    material_ = rendering::Material::Create();
    material_->blend = rendering::BlendMode::Additive;
  }

  // Blending, and the quads' shape when it has a texture (see above).
  void SetMaterial(std::shared_ptr<rendering::Material> material) { material_ = std::move(material); }
  const std::shared_ptr<rendering::Material>& GetMaterial() const { return material_; }

  // This frame's quads: the returned span (`count` long, at most Capacity()) is written in place, so nothing is
  // copied on the way to the GPU upload. Contents beyond the previous count are unspecified. Marks the quads as
  // changed (re-uploaded before the next draw).
  std::span<rendering::BillboardInstance> Write(size_t count) {
    billboards_.resize(std::min(count, capacity_));  // within the reserve: no allocation
    ++version_;
    return billboards_;
  }
  void Clear() { Write(0); }

  // The box around the quads in node units, for frustum culling: like Unity's particle bounds or Godot's
  // visibility_aabb, the renderer trusts it instead of visiting every quad. FitBounds() computes it from the quads
  // (rendering::BillboardBounds: sizes and stretches included).
  void SetBounds(const AABB& box) { bounds_ = box; }
  void FitBounds() { bounds_ = rendering::BillboardBounds(billboards_); }
  const AABB& Bounds() const { return bounds_; }
  // The bounds in world space (through the node's transform and its parents').
  AABB WorldBounds() const { return TransformBox(GetWorldMatrix(), bounds_); }

  std::span<const rendering::BillboardInstance> Billboards() const { return billboards_; }
  size_t Count() const { return billboards_.size(); }
  size_t Capacity() const { return capacity_; }
  bool Empty() const { return billboards_.empty(); }

  class InternalAccess;
  // Engine-only parts (the renderer's): see InternalAccess below.
  InternalAccess Internal();

 private:
  size_t capacity_;
  std::shared_ptr<rendering::Material> material_;
  std::vector<rendering::BillboardInstance> billboards_;
  uint64_t version_ = 0;
  AABB bounds_{};
  std::unique_ptr<rendering::WInstanceBuffer> buffer_;
};

// For the renderer, not games.
class BillboardBatch::InternalAccess {
 public:
  explicit InternalAccess(BillboardBatch& batch) : batch_(batch) {}

  uint64_t Version() const { return batch_.version_; }  // bumped by every Write
  rendering::WInstanceBuffer* GetBuffer() const { return batch_.buffer_.get(); }
  void SetBuffer(std::unique_ptr<rendering::WInstanceBuffer> buffer) const { batch_.buffer_ = std::move(buffer); }

 private:
  BillboardBatch& batch_;
};

inline BillboardBatch::InternalAccess BillboardBatch::Internal() { return InternalAccess(*this); }

}  // namespace wlw::scene
