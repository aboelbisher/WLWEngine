// The canvas's quads. One shader for every kind, so a run of quads is one draw call; the kind
// is the same for a whole quad, so the branch costs next to nothing. Output: premultiplied alpha.
// The kinds match ui::QuadKind (canvas.h); a test checks they agree.
#include "wlw/color.glsl"

#define KIND_TEXT 1
#define KIND_RECT 2
#define KIND_IMAGE 4
#define KIND_TRIANGLE 5
#define KIND_ARC 6

in highp vec2 vUV;
in mediump vec4 vColor;
in highp vec4 vParams;

uniform mediump sampler2D uTexture;  // unit 0: the run's font atlas, image, or a white texel

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
  highp vec2 q = abs(vUV) - vParams.yz + vParams.w;
  highp float d = length(max(q, 0.0)) + min(max(q.x, q.y), 0.0) - vParams.w;
  return clamp(0.5 - d / max(fwidth(d), 1e-4), 0.0, 1.0);
}

// A triangle: vUV.x, vUV.y and vParams.y = the distances to its three edges (positive inside),
// interpolated exactly because they're linear; the nearest edge decides.
float TriangleCoverage() {
  highp float d = min(min(vUV.x, vUV.y), vParams.y);
  return clamp(0.5 + d / max(fwidth(d), 1e-4), 0.0, 1.0);
}

// Part of a ring with round ends (Inigo Quilez's sdArc): vUV = the position from the centre in a
// frame whose +y points at the middle of the arc; vParams.y = radius, z = half thickness, w = half
// the sweep (pi = a whole ring).
float ArcCoverage() {
  highp vec2 p = vec2(abs(vUV.x), vUV.y);
  highp vec2 sc = vec2(sin(vParams.w), cos(vParams.w));
  highp float d = (sc.y * p.x > sc.x * p.y) ? length(p - sc * vParams.y) : abs(length(p) - vParams.y);
  d -= vParams.z;
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
  float triangle = TriangleCoverage();
  float arc = ArcCoverage();
  int kind = int(vParams.x + 0.5);
  if (kind == KIND_IMAGE) {
    FragColor = Premultiply(ImageColor());
  } else {
    float coverage = kind == KIND_TEXT ? text : kind == KIND_TRIANGLE ? triangle : kind == KIND_ARC ? arc : rect;
    FragColor = Premultiply(vec4(vColor.rgb, vColor.a * coverage));
  }
}
