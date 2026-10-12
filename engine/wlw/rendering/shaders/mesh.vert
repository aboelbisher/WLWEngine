// The engine's 3D shader, vertex stage. Variants (defines, see rendering/mesh_variant.h):
//   INSTANCED  many copies in one draw: per-copy transform and tint
//   TEXTURED   the material has a texture
//   LIT        the scene has light nodes
//   SHADOWS    receives the shadow light's shadow (LIT too)
//   BLEND      the material blends (not Opaque)
#include "wlw/frame.glsl"
#ifdef SHADOWS
#include "wlw/shadows.glsl"
#endif

layout (location = 0) in vec3 aPos;
layout (location = 1) in vec4 aColor;
layout (location = 2) in vec3 aNormal;
layout (location = 3) in vec2 aUV;
#ifdef INSTANCED
// Per INSTANCE (glVertexAttribDivisor 1): they advance once per copy. A mat4 takes 4 locations.
layout (location = 4) in mat4 aInstanceTransform;
layout (location = 8) in vec4 aInstanceTint;
#endif

uniform mat4 uModel;
#ifndef INSTANCED
uniform mat3 uNormalMatrix;  // transpose(inverse(model)), computed once on the CPU
#endif

out vec4 vColor;      // rgb + alpha (instanced: times the copy's tint)
out vec3 vNormal;     // world
out vec3 vWorldPos;
out vec2 vUV;
#ifdef SHADOWS
out vec4 vLightSpacePos;  // where this vertex is in the shadow map
#endif

void main() {
#ifdef INSTANCED
  mat4 world = uModel * aInstanceTransform;
  mat3 normal_matrix = mat3(transpose(inverse(world)));  // per vertex: copies may be squashed
  vColor = aColor * aInstanceTint;
#else
  mat4 world = uModel;
  mat3 normal_matrix = uNormalMatrix;
  vColor = aColor;
#endif
  vec4 world_pos = world * vec4(aPos, 1.0);
  vWorldPos = world_pos.xyz;
  vNormal = normalize(normal_matrix * aNormal);
  vUV = aUV;
  gl_Position = uViewProjection * world_pos;
#ifdef SHADOWS
  vLightSpacePos = ShadowLookupPosition(world_pos.xyz, vNormal);
#endif
}
