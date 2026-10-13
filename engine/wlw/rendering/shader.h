#pragma once

#include <memory>
#include <string>

#include "wlw/rendering/gpu_resource.h"

namespace wlw::rendering {

// A game's own GPU program, used through a ShaderMaterial: on 3D nodes and meshes (mesh shaders),
// on Canvas::ShaderRect (canvas shaders) or as a viewport's post pass (post shaders). Made from one text by Shader::Create (or FromFile); if
// it doesn't compile, the error is logged and IsValid() is false (a 3D node then draws with the
// engine's own shader; a ShaderRect draws nothing).
//
// The text (GLSL, no #version line: see rendering/gl.h), like a Godot shader:
//
//   shader_type canvas;            // or mesh, or post   (required, first)
//   uniform float uTime;           // the game's uniforms (ShaderMaterial::SetParam) and helpers
//   void fragment() {              // required: sets FragColor
//     FragColor = vec4(vUV, 0.0, 1.0);
//   }
//   void vertex() { ... }          // optional, mesh shaders only: sets gl_Position and the v* outputs
//
// The engine declares its inputs before the text (shaders/custom/*) and adds main(). The text may
// #include shader files: the engine's (wlw/lighting.glsl, wlw/shadows.glsl, wlw/common.glsl,
// wlw/color.glsl; see shader_library.h) and the game's own (AddShaderFile). Each stage gets the whole text
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
//   Both stages:          uniform mat4 uModel; the frame block (wlw/frame.glsl): uView, uProjection,
//                         uViewProjection, uCameraPos (vec3), the lights, the shadow light...
//   vertex() gets:        in aPos, aColor (vec4), aNormal, aUV; aInstanceTransform (mat4),
//                         aInstanceTint (vec4); uniform bool uInstanced;
//                         sets gl_Position and out vWorldPos, vNormal, vUV, vColor.
//   fragment() gets:      in vec3 vWorldPos; in vec3 vNormal (world, not normalized);
//                         in vec2 vUV; in vec4 vColor; out vec4 FragColor;
//                         FragColor is premultiplied alpha (rgb * a); it only blends if the
//                         material's `blend` isn't Opaque (see blend_mode.h).
//   Lighting: #include "wlw/lighting.glsl" and multiply by SceneLighting(vWorldPos,
//   normalize(vNormal), ViewDirection(vWorldPos), shininess, 1.0): the scene's lights, like the
//   engine's shader. In the shadow pass the node is drawn as its plain mesh (a vertex() that
//   moves vertices isn't seen there).
//
// Post shaders (a viewport's post passes, see rendering/post_pass.h; no vertex()). fragment() runs
// once per pixel of the pass's output and gets:
//   in vec2 vUV;              // 0..1 across the picture, (0, 0) = BOTTOM left, like the textures:
//                             // texture(uSource, vUV) is the same pixel
//   uniform sampler2D uSource;  // the previous pass's output (the first pass: the viewport's picture)
//   uniform sampler2D uScene;   // the viewport's picture before any pass
//   uniform vec2 uTexel;        // 1 / uSource's size in pixels: the step to a neighbouring texel
//   uniform vec2 uOutputSize;   // this pass's output size in pixels
//   out vec4 FragColor;         // replaces the output pixel (no blending), premultiplied alpha
// plus a sampler2D uniform for each of the pass's `inputs` (other passes' outputs, by name).
enum class ShaderTarget { Mesh, Canvas, Post };

// A shader text split for the two GPU stages (no GPU needed; the backends add their inputs).
struct ShaderSource {
  bool ok = false;
  std::string error;  // why not ok: no/unknown shader_type, no fragment(), unbalanced braces...
  ShaderTarget target = ShaderTarget::Canvas;
  std::string fragment;  // the text without vertex(), + a main() that calls fragment()
  std::string vertex;    // the text without fragment(), + a main() that calls vertex(); empty = no vertex()
};
// Removed parts become blank lines, so line numbers stay the game's.
ShaderSource SplitShaderSource(const std::string& code);

class Shader : public Resource {
 public:
  using Target = ShaderTarget;

  // From its text. Checked now (shader_type, fragment(), braces: errors are logged and IsValid()
  // is false); compiled by the renderer before the first frame (warm-up) or when first drawn.
  static std::shared_ptr<Shader> Create(const std::string& code, const std::string& name = "custom shader");
  // From a file (read with platform::ReadFile, so it works inside the APK too). A missing file is
  // logged and gives an invalid shader.
  static std::shared_ptr<Shader> FromFile(const std::string& path);

  Target GetTarget() const { return source_.target; }
  // False if its text is wrong or it didn't compile (both logged).
  bool IsValid() const { return source_.ok && !failed_; }
  const std::string& Name() const { return name_; }  // in error messages

  // ---- For the renderer ----
  const ShaderSource& Source() const { return source_; }
  void MarkFailed() const { failed_ = true; }  // didn't compile: drawn as invalid from now on

  Shader(ShaderSource source, std::string name);  // use Create

 private:
  ShaderSource source_;
  std::string name_;
  mutable bool failed_ = false;
};

}  // namespace wlw::rendering
