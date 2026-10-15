// The shadow pass: only depth, seen from the shadow light. Variant: INSTANCED.
#include "wlw/frame.glsl"

layout (location = 0) in vec3 aPos;
#ifdef INSTANCED
layout (location = 4) in mat4 aInstanceTransform;
#endif

uniform highp mat4 uModel;

void main() {
#ifdef INSTANCED
  // Matrix-vector products only (multiplying the matrices would happen per vertex).
  gl_Position = uLightSpace * (uModel * (aInstanceTransform * vec4(aPos, 1.0)));
#else
  gl_Position = uLightSpace * uModel * vec4(aPos, 1.0);  // compilers hoist the uniform-only product
#endif
}
