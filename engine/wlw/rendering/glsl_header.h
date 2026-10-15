#pragma once

#include <string>

#include "wlw/core/cvar.h"

// The text put in front of every shader stage: the #version line and, in GLSL ES, the default
// precisions. Shader sources have no #version line (rendering/gl.h); the GL backend picks the
// dialect from the context (GLES on phones, or a desktop OpenGL ES context: tests/gpu).
//
// Precision (GLSL ES only; desktop GL ignores it): highp = 32-bit floats, mediump = at least 16-bit
// (real fp16 on Mali, Adreno, PowerVR and Apple GPUs). fp16 has 11 significant bits and tops out at
// 65504, so positions, matrices, texture / pixel / canvas coordinates, depth and shadow coordinates,
// distances and the specular term need highp.
//   floats and ints: highp in both stages. A shader opts in to 16-bit maths with its own
//             `precision mediump float;` (games' shaders may, see rendering/shader.h) where a phone
//             measured it faster. Why not mediump by default: on the Pixel 4 (Adreno 640) Bluzzle's lit
//             3D pass took 8% longer with a mediump fragment default (starting mediump then switching
//             back to highp was slower still), and the post effects' opt-in 2% (Roadmap 1.3).
//   samplers, the same in both stages (a sampler declared in both must match): sampler2D, 3D, Cube,
//             2DArray mediump (fp16 holds RGBA8 and RGBA16F exactly); shadow and integer samplers highp.
//             GLSL ES has no default for 3D, array, shadow or integer samplers, and sampler2D's is lowp.
// A uniform (or uniform-block member) declared in both stages must have the same precision in both:
// the engine's shader files name the precision of every uniform, block member and varying (a test
// checks), so they stay correct whatever default the code after them uses.
namespace wlw::rendering {

enum class GlslDialect { Glsl330, Essl300 };  // desktop OpenGL 3.3 core / OpenGL ES 3.0
enum class ShaderStage { Vertex, Fragment };

// r.mediump: 0 = shaders' own `precision mediump float;` opt-ins are ignored (compiled highp): A/B a
// phone's GPU time, or check whether an artifact is a precision one. Read when programs compile, so
// it's set before start.
inline CVar<bool> cv_mediump{"r.mediump", true, "Shaders that opt in to mediump floats get them (0 = all highp)",
                             CVarFlags::ReadOnly};

// The header for one stage.
std::string GlslHeader(GlslDialect dialect, ShaderStage stage);

// A stage's code with each `precision mediump float;` statement made highp when `allow_mediump` is
// false (r.mediump 0). Mediump on single declarations (varyings, block members) stays: both stages
// have to agree on those.
std::string ApplyMediumpSetting(std::string code, bool allow_mediump);

}  // namespace wlw::rendering
