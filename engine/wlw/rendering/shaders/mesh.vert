// The engine's 3D shader, vertex stage. Variants (defines, see rendering/mesh_variant.h):
//   INSTANCED  many copies in one draw: per-copy transform and tint
//   TEXTURED   the material has a texture
//   LIT        the scene has light nodes
//   SHADOWS    receives the shadow light's shadow (LIT too)
//   BLEND      the material blends (not Opaque)
#include "wlw/common.glsl"
#include "wlw/frame.glsl"
#ifdef SHADOWS
#include "wlw/shadows.glsl"
#endif
#ifdef INSTANCED
#include "wlw/instance.glsl"  // aInstanceRow0..2 (locations 4-6), aInstanceTint (7)
#endif

layout (location = 0) in vec3 aPos;
layout (location = 1) in vec4 aColor;
layout (location = 2) in vec3 aNormal;
layout (location = 3) in vec2 aUV;

uniform highp mat4 uModel;
uniform highp mat3 uNormalMatrix;  // core::NormalMatrix(uModel), once per node on the CPU

// (Varyings match the fragment stage by name, so their order is free: this one keeps Mali-G78's
// unlit INSTANCED variant at 32 work registers = full occupancy; vColor first takes 50, half.)
out highp vec2 vUV;
out highp vec3 vWorldPos;
out mediump vec3 vNormal;  // world
out mediump vec4 vColor;   // rgb + alpha (instanced: times the copy's tint)
#ifdef SHADOWS
out highp vec4 vLightSpacePos;  // where this vertex is in the shadow map
#endif

void main() {
#ifdef INSTANCED
  // The copy's transform, then the node's (vector products: cheaper than multiplying the matrices
  // per vertex). The copy's part of the normal matrix per vertex: copies may be squashed or mirrored.
  highp vec4 world_pos = uModel * vec4(InstancePosition(aPos), 1.0);
  highp vec3 normal = SafeNormalize(uNormalMatrix * InstanceNormal(aNormal));
  vColor = aColor * aInstanceTint;
#else
  highp vec4 world_pos = uModel * vec4(aPos, 1.0);
  highp vec3 normal = SafeNormalize(uNormalMatrix * aNormal);
  vColor = aColor;
#endif
  vWorldPos = world_pos.xyz;
  vNormal = normal;
  vUV = aUV;
  gl_Position = uViewProjection * world_pos;
#ifdef SHADOWS
  vLightSpacePos = ShadowLookupPosition(world_pos.xyz, normal);
#endif
}
