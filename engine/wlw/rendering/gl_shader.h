#pragma once

#include <memory>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include <glm/glm.hpp>

#include "wlw/rendering/gl.h"
#include "wlw/rendering/gpu_resource.h"
#include "wlw/rendering/shader_material.h"
#include "wlw/rendering/shader.h"

namespace wlw::rendering {

// A game's Shader on GL (its GpuResource, made by GLShaderFor in gl_resources.h): the engine's
// inputs (see shader.h) + each stage of the game's text, compiled and linked into one program.
// Used by the 3D renderer and the UI renderer.
//
// A program keeps its uniforms' values between draws, so uploads only go out when they change: a material's params
// when another material (or a changed one) is drawn with it, an instance uniform when its value differs from the
// last draw's.
class GLShader : public GpuResource {
 public:
  explicit GLShader(const Shader& shader);  // compiles it; Program() is 0 if that failed (logged)
  ~GLShader() override;

  GLuint Program() const { return program_; }
  // A uniform's location (-1 if the program has none by that name), looked up once.
  GLint Location(const std::string& name) const;

  // The engine's inputs (shader.h), looked up once after linking (-1 = the program doesn't use it).
  struct Inputs {
    GLint model = -1, normal_matrix = -1, instanced = -1, receives_shadow = -1, shadow_map = -1;  // mesh
    GLint projection = -1, size = -1;                                                             // canvas
    GLint source = -1, scene = -1, texel = -1, output_size = -1;                                  // post
  };
  const Inputs& EngineInputs() const { return inputs_; }

  // The material's params on this program (in use): uploaded only if the program doesn't have their current values
  // yet; its textures are bound to units first_unit, first_unit + 1, ... every time (units are shared by every
  // program). Instance uniforms aren't uploaded here (ApplyInstanceParams). Returns the next free texture unit.
  int ApplyParams(const ShaderMaterial& material, int first_unit) const;
  // The shader's instance uniforms for one draw (after ApplyParams): `node`'s value, else the material's, else 0.
  // Each is uploaded only when it differs from the program's current value.
  void ApplyInstanceParams(const InstanceParams* node) const;

 private:
  struct InstanceSlot {
    std::string name;
    InstanceUniformType type = InstanceUniformType::Float;
    GLint location = -1;
    glm::vec4 fallback{0.0f};  // the material's value (else 0), set by ApplyParams
    glm::vec4 uploaded{0.0f};  // what the program has (a linked program's uniforms start at 0)
  };

  GLuint program_ = 0;
  mutable std::unordered_map<std::string, GLint> locations_;
  Inputs inputs_;
  mutable std::vector<InstanceSlot> instance_slots_;
  // The material whose params the program has: (id, version, first texture unit); id 0 = none yet.
  mutable uint64_t material_id_ = 0;
  mutable uint64_t material_version_ = 0;
  mutable int material_unit_ = -1;
  mutable std::vector<const WTexture*> material_textures_;  // its textures, in unit order (bound on every use)
};

}  // namespace wlw::rendering
