// The canvas (ui::Canvas): batched quads in canvas units.
#include "wlw/color.glsl"

layout (location = 0) in vec2 aPos;
layout (location = 1) in vec2 aUV;
layout (location = 2) in vec4 aColor;
layout (location = 3) in vec4 aParams;  // x: the quad's kind (ui.frag); the rest depends on it

uniform highp mat4 uProjection;  // canvas units -> clip space

out highp vec2 vUV;      // canvas units: a 600-unit arc radius would move in half-unit steps in fp16
out mediump vec4 vColor;
out highp vec4 vParams;
// The same for the whole quad, so not interpolated: the kind, and an arc's sin/cos of its half sweep
// (computed here per corner instead of per pixel).
flat out mediump int vKind;
flat out highp vec2 vArcSinCos;

void main() {
  gl_Position = uProjection * vec4(aPos, 0.0, 1.0);
  vUV = aUV;
  // Premultiplied here, before the GPU interpolates it across the quad: a gradient from a colour to transparent
  // then fades evenly instead of darkening in the middle (straight colours interpolated, then premultiplied, would
  // dip). The same as premultiplying per pixel for quads of one colour.
  vColor = Premultiply(aColor);
  vParams = aParams;
  vKind = int(aParams.x + 0.5);
  vArcSinCos = vec2(sin(aParams.w), cos(aParams.w));
}
