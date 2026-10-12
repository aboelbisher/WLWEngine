// The canvas's quads. One shader for every kind, so a run of quads is one draw call; the kind
// is the same for a whole quad, so the branch costs next to nothing. Output: premultiplied alpha.
// The kinds match ui::QuadKind (canvas.h); a test checks they agree.
#include "wlw/color.glsl"

#define KIND_TEXT 1
#define KIND_RECT 2
#define KIND_IMAGE 4

in vec2 vUV;
in vec4 vColor;
in vec4 vParams;

uniform sampler2D uTexture;  // unit 0: the run's font atlas, image, or a white texel

out vec4 FragColor;

// SDF text: the atlas stores the distance to the glyph's edge (0.5 = the edge); vParams.y is where
// to cut (lower = thicker, for outlines). fwidth() = how much it changes per pixel, so the edge is
// anti-aliased over ~1 pixel at any size.
float TextCoverage() {
  float d = texture(uTexture, vUV).r;
  float w = max(fwidth(d) * 0.7, 1e-4);
  return smoothstep(vParams.y - w, vParams.y + w, d);
}

// A rectangle with rounded corners: vUV = the position from its centre, vParams.yz = its half
// size, vParams.w = the corner radius; the signed distance to a rounded box, anti-aliased.
float RoundedRectCoverage() {
  vec2 q = abs(vUV) - vParams.yz + vParams.w;
  float d = length(max(q, 0.0)) + min(max(q.x, q.y), 0.0) - vParams.w;
  return clamp(0.5 - d / max(fwidth(d), 1e-4), 0.0, 1.0);
}

// An image: the texel (straight alpha, like image files) times the tint.
vec4 ImageColor() {
  return texture(uTexture, vUV) * vColor;
}

void main() {
  // Both coverages first, outside any branch: fwidth() needs every pixel of a 2x2 block to run
  // it, and neighbouring pixels can belong to different quads.
  float text = TextCoverage();
  float rect = RoundedRectCoverage();
  int kind = int(vParams.x + 0.5);
  if (kind == KIND_IMAGE) {
    FragColor = Premultiply(ImageColor());
  } else {
    float coverage = kind == KIND_TEXT ? text : rect;
    FragColor = Premultiply(vec4(vColor.rgb, vColor.a * coverage));
  }
}
