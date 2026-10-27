#pragma once

#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include <glm/glm.hpp>

#include "wlw/core/color.h"
#include "wlw/rendering/material.h"
#include "wlw/rendering/shader.h"
#include "wlw/rendering/texture.h"

namespace wlw::rendering {

// A material drawn by a game's own Shader (see shader.h) and that shader's uniforms ("params").
// Like Godot's ShaderMaterial: give it to a 3D node or mesh (SetMaterial) with a Target::Mesh
// shader, or to Canvas::ShaderRect with a Target::Canvas one. Several materials can share one
// shader, each with its own params. Values that differ per node (one block flashing) are the shader's
// `instance uniform`s instead (shader.h): one material, each node its own value.
//
// The engine look's settings it inherits from Material (texture, lighting, shininess) don't apply:
// the shader decides everything; pass textures as params.
class ShaderMaterial : public Material {
 public:
  // A value for a uniform of the same type; the vectors fill a uniform array (`uniform vec4 uRings[4];`) from its
  // first element (fewer values leave the rest as they were, more are cut at the array's size).
  using Param = std::variant<float, glm::vec2, glm::vec3, glm::vec4, glm::mat4, std::shared_ptr<WTexture>,
                             std::vector<float>, std::vector<glm::vec4>>;

  // Blending starts as the shader's kind expects (like Godot): canvas shaders Alpha (they draw
  // over the UI), mesh shaders Opaque. Change `blend` as needed (see blend_mode.h).
  explicit ShaderMaterial(std::shared_ptr<Shader> shader) : shader_(std::move(shader)) {
    if (shader_ && shader_->GetTarget() == Shader::Target::Canvas) blend = BlendMode::Alpha;
  }
  static std::unique_ptr<ShaderMaterial> Create(std::shared_ptr<Shader> shader) {
    return std::make_unique<ShaderMaterial>(std::move(shader));
  }

  const std::shared_ptr<Shader>& GetShader() const { return shader_; }
  // Drawable: a shader that compiled. Otherwise nothing is drawn with this material.
  bool IsValid() const { return shader_ && shader_->IsValid(); }

  // A uniform of the shader, by name (e.g. SetParam("uTime", t)); setting a name again replaces
  // its value. Names the shader doesn't have are ignored. Textures get their own texture units.
  // The renderer uploads the params only after a change, so setting the same value every frame is free.
  void SetParam(const std::string& uniform, Param value) {
    for (auto& [key, existing] : params_) {
      if (key == uniform) {
        if (existing == value) return;
        existing = std::move(value);
        ++version_;
        return;
      }
    }
    params_.emplace_back(uniform, std::move(value));
    ++version_;
  }
  // A colour goes in as a vec4 (r, g, b, a), as authored (see color.h).
  void SetParam(const std::string& uniform, const Color& color) { SetParam(uniform, Param(color.ToVec4())); }
  const Param* GetParam(const std::string& uniform) const {
    for (const auto& [key, value] : params_) {
      if (key == uniform) return &value;
    }
    return nullptr;
  }
  const std::vector<std::pair<std::string, Param>>& Params() const { return params_; }

  const ShaderMaterial* AsShaderMaterial() const override { return this; }

  class InternalAccess;
  InternalAccess Internal() const;

 private:
  // Unique per material for the program's life (never reused); a copy is a new material and gets its own.
  struct Id {
    uint64_t value = Next();
    Id() = default;
    Id(const Id&) : value(Next()) {}
    Id& operator=(const Id&) {
      value = Next();
      return *this;
    }
    static uint64_t Next() {
      static std::atomic<uint64_t> next{1};
      return next.fetch_add(1, std::memory_order_relaxed);
    }
  };

  std::shared_ptr<Shader> shader_;
  std::vector<std::pair<std::string, Param>> params_;
  Id id_;
  uint64_t version_ = 0;  // bumped when a param is added or changes value
};

// For the renderer, not games.
class ShaderMaterial::InternalAccess {
 public:
  explicit InternalAccess(const ShaderMaterial& material) : material_(material) {}

  // (Id, Version) names the params' current values: the same pair = nothing to upload again.
  uint64_t Id() const { return material_.id_.value; }
  uint64_t Version() const { return material_.version_; }

 private:
  const ShaderMaterial& material_;
};

inline ShaderMaterial::InternalAccess ShaderMaterial::Internal() const { return InternalAccess(*this); }

}  // namespace wlw::rendering
