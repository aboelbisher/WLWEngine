#pragma once

#include <memory>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include <glm/glm.hpp>

#include "wlw/rendering/material.h"
#include "wlw/rendering/shader.h"
#include "wlw/rendering/texture.h"

namespace wlw::rendering {

// A material drawn by a game's own Shader (see shader.h) and that shader's uniforms ("params").
// Like Godot's ShaderMaterial: give it to a 3D node or mesh (SetMaterial) with a Target::Mesh
// shader, or to Canvas::ShaderRect with a Target::Canvas one. Several materials can share one
// shader, each with its own params.
//
// The engine look's settings it inherits from Material (texture, lighting, shininess) don't apply:
// the shader decides everything; pass textures as params.
class ShaderMaterial : public Material {
 public:
  using Param = std::variant<float, glm::vec2, glm::vec3, glm::vec4, glm::mat4, std::shared_ptr<WTexture>>;

  // Blending starts as the shader's kind expects (like Godot): canvas shaders Alpha (they draw
  // over the UI), mesh shaders Opaque. Change `blend` as needed (see blend_mode.h).
  explicit ShaderMaterial(std::shared_ptr<Shader> shader) : shader_(std::move(shader)) {
    if (shader_ && shader_->GetTarget() == Shader::Target::Canvas) blend = BlendMode::Alpha;
  }
  static std::shared_ptr<ShaderMaterial> Create(std::shared_ptr<Shader> shader) {
    return std::make_shared<ShaderMaterial>(std::move(shader));
  }

  const std::shared_ptr<Shader>& GetShader() const { return shader_; }
  // Drawable: a shader that compiled. Otherwise nothing is drawn with this material.
  bool IsValid() const { return shader_ && shader_->IsValid(); }

  // A uniform of the shader, by name (e.g. SetParam("uTime", t)); setting a name again replaces
  // its value. Names the shader doesn't have are ignored. Textures get their own texture units.
  void SetParam(const std::string& uniform, Param value) {
    for (auto& [key, existing] : params_) {
      if (key == uniform) {
        existing = std::move(value);
        return;
      }
    }
    params_.emplace_back(uniform, std::move(value));
  }
  const Param* GetParam(const std::string& uniform) const {
    for (const auto& [key, value] : params_) {
      if (key == uniform) return &value;
    }
    return nullptr;
  }
  const std::vector<std::pair<std::string, Param>>& Params() const { return params_; }

  const ShaderMaterial* AsShaderMaterial() const override { return this; }

 private:
  std::shared_ptr<Shader> shader_;
  std::vector<std::pair<std::string, Param>> params_;
};

}  // namespace wlw::rendering
