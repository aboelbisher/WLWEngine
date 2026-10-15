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

layout (location = 0) in vec3 aPos;
layout (location = 1) in vec4 aColor;
layout (location = 2) in vec3 aNormal;
layout (location = 3) in vec2 aUV;
#ifdef INSTANCED
// Per INSTANCE (glVertexAttribDivisor 1): they advance once per copy. A mat4 takes 4 locations.
layout (location = 4) in mat4 aInstanceTransform;
layout (location = 8) in vec4 aInstanceTint;
#endif

uniform highp mat4 uModel;
uniform highp mat3 uNormalMatrix;  // core::NormalMatrix(uModel), once per node on the CPU

out mediump vec4 vColor;   // rgb + alpha (instanced: times the copy's tint)
out mediump vec3 vNormal;  // world
out highp vec3 vWorldPos;
out highp vec2 vUV;
#ifdef SHADOWS
out highp vec4 vLightSpacePos;  // where this vertex is in the shadow map
#endif

void main() {
#ifdef INSTANCED
  // The copy's transform, then the node's (matrix-vector products: cheaper than multiplying the
  // matrices per vertex). The copy's part of the normal matrix per vertex: copies may be squashed or
  // mirrored.
  highp vec4 world_pos = uModel * (aInstanceTransform * vec4(aPos, 1.0));
  highp vec3 normal = SafeNormalize(uNormalMatrix * (NormalMatrix(mat3(aInstanceTransform)) * aNormal));
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
