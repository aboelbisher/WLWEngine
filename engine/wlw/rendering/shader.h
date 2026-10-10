#pragma once

#include <string>

namespace wlw::rendering {

// A game's own GPU program, used through a ShaderMaterial: on 3D nodes and meshes (mesh shaders)
// or on Canvas::ShaderRect (canvas shaders). Made from one text by RenderDevice::CreateShader; if
// it doesn't compile, the error is logged and IsValid() is false (a 3D node then draws with the
// engine's own shader; a ShaderRect draws nothing).
//
// The text (GLSL, no #version line: see rendering/gl.h), like a Godot shader:
//
//   shader_type canvas;            // or: shader_type mesh;   (required, first)
//   uniform float uTime;           // the game's uniforms (ShaderMaterial::SetParam) and helpers
//   void fragment() {              // required: sets FragColor
//     FragColor = vec4(vUV, 0.0, 1.0);
//   }
//   void vertex() { ... }          // optional, mesh shaders only: sets gl_Position and the v* outputs
//
// The engine declares its inputs before the text and adds main(). Each stage gets the whole text
// minus the other stage's function, so shared helpers must compile in both stages when there is a
// vertex(): keep fragment-only built-ins (dFdx, fwidth, gl_FragCoord) inside fragment().
// Error line numbers match the game's text.
//
// Canvas shaders (the vertex stage is always the engine's). fragment() gets:
//   in vec2 vUV;          // 0..1 across the rectangle, (0, 0) = top left
//   in vec4 vColor;       // the ShaderRect's tint
//   uniform vec2 uSize;   // the rectangle's size in pixels
//   out vec4 FragColor;   // premultiplied alpha (rgb * a), like the rest of the UI
//
// Mesh shaders. Without vertex() the engine's does the usual transform (instancing included).
//   Both stages:          uniform mat4 uModel, uView, uProjection; uniform vec3 uCameraPos;
//   vertex() gets:        in aPos, aColor (vec4), aNormal, aUV; aInstanceTransform (mat4),
//                         aInstanceTint (vec4); uniform bool uInstanced;
//                         sets gl_Position and out vWorldPos, vNormal, vUV, vColor.
//   fragment() gets:      in vec3 vWorldPos; in vec3 vNormal (world, not normalized);
//                         in vec2 vUV; in vec4 vColor; out vec4 FragColor;
//   The scene's lights and shadows aren't passed in (yet): light it in the shader. In the shadow
//   pass the node is drawn as its plain mesh (a vertex() that moves vertices isn't seen there).
class Shader {
 public:
  enum class Target { Mesh, Canvas };

  virtual ~Shader() = default;
  Target GetTarget() const { return target_; }
  bool IsValid() const { return valid_; }

 protected:
  Shader() = default;
  Target target_ = Target::Canvas;
  bool valid_ = false;
};

// A shader text split for the two GPU stages (no GPU needed; the backends add their inputs).
struct ShaderSource {
  bool ok = false;
  std::string error;  // why not ok: no/unknown shader_type, no fragment(), unbalanced braces...
  Shader::Target target = Shader::Target::Canvas;
  std::string fragment;  // the text without vertex(), + a main() that calls fragment()
  std::string vertex;    // the text without fragment(), + a main() that calls vertex(); empty = no vertex()
};
// Removed parts become blank lines, so line numbers stay the game's.
ShaderSource SplitShaderSource(const std::string& code);

}  // namespace wlw::rendering
