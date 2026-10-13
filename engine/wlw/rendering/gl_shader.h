#pragma once

#include <memory>
#include <string>
#include <unordered_map>

#include "wlw/rendering/gl.h"
#include "wlw/rendering/gpu_resource.h"
#include "wlw/rendering/shader_material.h"
#include "wlw/rendering/shader.h"

namespace wlw::rendering {

// A game's Shader on GL (its GpuResource, made by GLShaderFor in gl_resources.h): the engine's
// inputs (see shader.h) + each stage of the game's text, compiled and linked into one program.
// Used by the 3D renderer and the UI renderer.
class GLShader : public GpuResource {
 public:
  explicit GLShader(const Shader& shader);  // compiles it; Program() is 0 if that failed (logged)
  ~GLShader() override;

  GLuint Program() const { return program_; }
  // A uniform's location (-1 if the program has none by that name), looked up once.
  GLint Location(const std::string& name) const;
  // Uploads the material's params to this program (in use); textures go to units first_unit,
  // first_unit + 1, ... Returns the next free texture unit.
  int ApplyParams(const ShaderMaterial& material, int first_unit) const;

 private:
  GLuint program_ = 0;
  mutable std::unordered_map<std::string, GLint> locations_;
};

}  // namespace wlw::rendering
