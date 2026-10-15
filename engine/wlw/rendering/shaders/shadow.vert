// The shadow pass: only depth, seen from the shadow light. Variant: INSTANCED.
#include "wlw/frame.glsl"

layout (location = 0) in vec3 aPos;
#ifdef INSTANCED
layout (location = 4) in mat4 aInstanceTransform;
#endif

uniform highp mat4 uModel;

void main() {
#ifdef INSTANCED
  mat4 world = uModel * aInstanceTransform;
#else
  mat4 world = uModel;
#endif
  gl_Position = uLightSpace * world * vec4(aPos, 1.0);
}
