// What a mesh shader's vertex() gets (see rendering/shader.h). Put in front of the game's text.
// The frame block gives uView, uProjection, uViewProjection, uCameraPos, the lights...
#include "wlw/common.glsl"
#include "wlw/frame.glsl"
#include "wlw/instance.glsl"  // aInstanceRow0..2, aInstanceTint, InstanceTransform(), InstancePosition/Normal

layout (location = 0) in vec3 aPos;
layout (location = 1) in vec4 aColor;
layout (location = 2) in vec3 aNormal;
layout (location = 3) in vec2 aUV;

uniform highp mat4 uModel;
uniform highp mat3 uNormalMatrix;  // carries the node's normals to world space (not unit length)
uniform bool uInstanced;

out highp vec3 vWorldPos;
out mediump vec3 vNormal;
out highp vec2 vUV;
out mediump vec4 vColor;

// The usual transforms, instanced copies included: a point of the mesh in world space, and a mesh
// normal in world space (unit length).
highp vec4 WorldPosition(highp vec3 local) {
  if (uInstanced) local = InstancePosition(local);
  return uModel * vec4(local, 1.0);
}
highp vec3 WorldNormal(highp vec3 normal) {
  if (uInstanced) normal = InstanceNormal(normal);
  return SafeNormalize(uNormalMatrix * normal);
}
