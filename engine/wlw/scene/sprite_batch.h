#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <span>
#include <vector>

#include <glm/glm.hpp>

#include "wlw/rendering/instance_buffer.h"
#include "wlw/rendering/material.h"
#include "wlw/scene/node.h"

namespace wlw::scene {

// Thousands of soft round dots in ONE draw call: glow, sparks, and liquid (overlapping additive dots
// add up into a density field; blur + threshold it and you get metaballs, see post_effects.h).
//
// Like Godot's MultiMesh with its compact 2D format, or a Niagara sprite renderer: each dot is a
// 16-byte SpriteInstance (position on the node's z = 0 plane, size, RGBA8 colour), and the GPU
// builds its quad in the vertex shader (shaders/sprite.vert), so the CPU only writes 16 bytes per
// dot per frame and the renderer makes one instanced call for all of them.
//
//   auto dots = std::make_shared<scene::SpriteBatch>(20000);
//   viewport->GetScene()->AddNode(dots);
//   // every frame (or let fx::FlowParticles::WriteTo do it):
//   std::span<rendering::SpriteInstance> out = dots->Write(count);
//   ... fill out[i] ...
//   dots->SetBounds(min, max);   // for culling: the box around the dots (sizes included)
//
// Its material decides blending (Additive by default: order-independent, so the dots are never
// sorted) and the dot's shape: a texture (e.g. utils::RadialFalloffTexture) or, without one, the
// same soft (1-d²)² falloff computed in the shader. A ShaderMaterial's shader isn't used (only its
// blend mode). Sprite batches don't cast or receive shadows and aren't lit.
class SpriteBatch : public Node3D {
 public:
  // `capacity`: the most dots it holds; reserved now, never grown (Write cuts at it).
  explicit SpriteBatch(size_t capacity = 4096) : capacity_(capacity) {
    sprites_.reserve(capacity);
    auto material = rendering::Material::Create();
    material->blend = rendering::BlendMode::Additive;
    SetMaterial(std::move(material));
  }

  SpriteBatch* AsSpriteBatch() override { return this; }

  // This frame's dots: the returned span (`count` long, at most Capacity()) is written in place, so
  // nothing is copied on the way to the GPU upload. Its old contents are unspecified. Marks the
  // dots as changed (re-uploaded before the next draw).
  std::span<rendering::SpriteInstance> Write(size_t count) {
    sprites_.resize(std::min(count, capacity_));  // within the reserve: no allocation
    ++version_;
    return sprites_;
  }
  void Clear() { Write(0); }

  // The box around the dots in node units, sizes included (min/max of position -+ size / 2), for
  // frustum culling: like Unity's particle bounds or Godot's visibility_rect, the renderer trusts it
  // instead of visiting every dot. FitBounds() computes it from the dots.
  void SetBounds(const glm::vec2& min, const glm::vec2& max) {
    bounds_min_ = min;
    bounds_max_ = max;
  }
  void FitBounds() {
    glm::vec2 lo(std::numeric_limits<float>::max()), hi(std::numeric_limits<float>::lowest());
    for (const rendering::SpriteInstance& s : sprites_) {
      lo = glm::min(lo, s.position - s.size * 0.5f);
      hi = glm::max(hi, s.position + s.size * 0.5f);
    }
    if (sprites_.empty()) lo = hi = glm::vec2(0.0f);
    SetBounds(lo, hi);
  }
  glm::vec2 BoundsMin() const { return bounds_min_; }
  glm::vec2 BoundsMax() const { return bounds_max_; }
  // The bounds in world space (through the node's transform and its parents').
  AABB WorldBounds() const {
    const glm::mat4 world = GetModelMatrix();
    glm::vec3 lo(std::numeric_limits<float>::max()), hi(std::numeric_limits<float>::lowest());
    for (const glm::vec2 corner : {bounds_min_, bounds_max_, glm::vec2(bounds_min_.x, bounds_max_.y),
                                   glm::vec2(bounds_max_.x, bounds_min_.y)}) {
      const glm::vec3 p(world * glm::vec4(corner, 0.0f, 1.0f));
      lo = glm::min(lo, p);
      hi = glm::max(hi, p);
    }
    return {{lo.x, lo.y, lo.z}, {hi.x, hi.y, hi.z}};
  }

  std::span<const rendering::SpriteInstance> Sprites() const { return sprites_; }
  size_t Count() const { return sprites_.size(); }
  size_t Capacity() const { return capacity_; }
  bool Empty() const { return sprites_.empty(); }

  // ---- For the renderer ----
  uint64_t Version() const { return version_; }  // bumped by every Write
  rendering::WInstanceBuffer* GetSpriteBuffer() const { return buffer_.get(); }
  void SetSpriteBuffer(std::unique_ptr<rendering::WInstanceBuffer> buffer) { buffer_ = std::move(buffer); }

 private:
  size_t capacity_;
  std::vector<rendering::SpriteInstance> sprites_;
  uint64_t version_ = 0;
  glm::vec2 bounds_min_{0.0f}, bounds_max_{0.0f};
  std::unique_ptr<rendering::WInstanceBuffer> buffer_;
};

}  // namespace wlw::scene
