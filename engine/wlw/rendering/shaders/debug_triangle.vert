// Debug draw's triangles (debug/debug_draw.h): world positions and colours already shaded on the CPU, 16 bytes a
// corner. Pulled towards the camera like the lines (debug_line.vert), so one lying on a surface stays visible.
#include "wlw/frame.glsl"

layout (location = 0) in vec3 aPosition;  // world
layout (location = 1) in vec4 aColor;     // RGBA8 read as 0..1, straight alpha

uniform highp float uDepthBias;  // the fraction of its distance a corner moves towards the camera

out mediump vec4 vColor;

void main() {
  highp vec4 view = uView * vec4(aPosition, 1.0);
  view.xyz *= uOrthographic > 0.5 ? vec3(1.0, 1.0, 1.0 - uDepthBias) : vec3(1.0 - uDepthBias);
  gl_Position = uProjection * view;
  vColor = aColor;
}
