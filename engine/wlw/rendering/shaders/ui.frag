// The canvas's quads. One shader for every kind, so a run of quads is one draw call. Each pixel
// computes only its own quad's kind, and each kind turns the uv's derivatives into its own
// anti-aliasing width (the chain rule) instead of calling fwidth() on every coverage function.
// Derivatives and texture reads happen inside the branches: the kind is a flat varying and the GPU
// shades each 2x2 pixel block from one triangle, so all four pixels of a block take the same branch
// (quad-uniform control flow: what Vulkan requires for derivatives; GLSL's spec calls it undefined,
// but every GPU computes them across the block). Output: premultiplied alpha.
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
flat in mediump int vKind;
flat in highp vec2 vArcSinCos;

uniform mediump sampler2D uTexture;  // unit 0: the run's font atlas, image, or a white texel

out vec4 FragColor;

// Coverage of a shape whose signed distance (canvas units, negative inside) is d, with gradient g
// in uv space: the chain rule gives d's change per screen pixel (|g . duv/dx| + |g . duv/dy| =
// fwidth(d)), so the edge fades over one pixel at any size.
float ShapeCoverage(highp float d, highp vec2 g, highp vec2 dx, highp vec2 dy) {
  highp float width = abs(dot(g, dx)) + abs(dot(g, dy));
  return clamp(0.5 - d / max(width, 1e-4), 0.0, 1.0);
}

// vec2(-1 or 1) per component (sign() would give 0 on an axis).
highp vec2 SignOf(highp vec2 v) { return mix(vec2(-1.0), vec2(1.0), step(0.0, v)); }

// SDF text: the atlas stores the distance to the glyph's edge (0.5 = the edge); vParams.y is where
// to cut (lower = thicker, for outlines), vParams.z = the field's range in atlas pixels (0 -> 1).
// The range in screen pixels (the formula MSDF uses) sets the width, so the edge is anti-aliased
// over ~1 pixel at any size.
float TextCoverage(highp vec2 dx, highp vec2 dy) {
  float d = texture(uTexture, vUV).r;
  highp vec2 unit_range = vec2(vParams.z) / vec2(textureSize(uTexture, 0));
  highp vec2 screen_tex_size = 1.0 / max(abs(dx) + abs(dy), vec2(1e-6));
  highp float screen_px_range = max(0.5 * dot(unit_range, screen_tex_size), 1.0);
  // +-0.9 px: what the earlier fwidth(d) x 0.7 gave on average over edge directions.
  float w = 0.9 / screen_px_range;
  return smoothstep(vParams.y - w, vParams.y + w, d);
}

// A rectangle with rounded corners: vUV = the position from its centre, vParams.yz = its half
// size, vParams.w = the corner radius; the signed distance to a rounded box, anti-aliased.
float RoundedRectCoverage(highp vec2 dx, highp vec2 dy) {
  highp vec2 q = abs(vUV) - vParams.yz + vParams.w;
  highp vec2 outside = max(q, 0.0);
  highp float outside_length = length(outside);
  highp float d = outside_length + min(max(q.x, q.y), 0.0) - vParams.w;
  // Outside the inner box: away from its nearest point; inside: across the nearest side.
  highp vec2 g = outside_length > 0.0 ? outside / outside_length : (q.x > q.y ? vec2(1.0, 0.0) : vec2(0.0, 1.0));
  return ShapeCoverage(d, g * SignOf(vUV), dx, dy);
}

// A triangle: vUV.x, vUV.y and vParams.y = the distances to its three edges (positive inside),
// interpolated exactly because they're linear; the nearest edge decides. dx, dy = their derivatives.
float TriangleCoverage(highp vec3 dx, highp vec3 dy) {
  highp float d = vUV.x;
  highp vec2 grad = vec2(dx.x, dy.x);
  if (vUV.y < d) {
    d = vUV.y;
    grad = vec2(dx.y, dy.y);
  }
  if (vParams.y < d) {
    d = vParams.y;
    grad = vec2(dx.z, dy.z);
  }
  return clamp(0.5 + d / max(abs(grad.x) + abs(grad.y), 1e-4), 0.0, 1.0);
}

// Part of a ring with round ends (Inigo Quilez's sdArc): vUV = the position from the centre in a
// frame whose +y points at the middle of the arc; vParams.y = radius, z = half thickness;
// vArcSinCos = sin and cos of half the sweep (pi = a whole ring).
float ArcCoverage(highp vec2 dx, highp vec2 dy) {
  highp vec2 p = vec2(abs(vUV.x), vUV.y);
  highp vec2 sc = vArcSinCos;
  highp float d;
  highp vec2 g;
  if (sc.y * p.x > sc.x * p.y) {  // past an end: the distance to its cap's centre
    highp vec2 to_cap = p - sc * vParams.y;
    d = length(to_cap);
    g = to_cap / max(d, 1e-6);
  } else {  // along the ring: the distance to its centre line
    highp float r = length(p);
    d = abs(r - vParams.y);
    g = p / max(r, 1e-6) * (r < vParams.y ? -1.0 : 1.0);
  }
  d -= vParams.z;
  return ShapeCoverage(d, g * vec2(vUV.x < 0.0 ? -1.0 : 1.0, 1.0), dx, dy);
}

void main() {
  if (vKind == KIND_IMAGE) {
    // The texel (straight alpha, like image files) times the tint.
    FragColor = Premultiply(texture(uTexture, vUV) * vColor);
    return;
  }
  highp vec2 dx = dFdx(vUV);
  highp vec2 dy = dFdy(vUV);
  float coverage;
  if (vKind == KIND_TEXT) {
    coverage = TextCoverage(dx, dy);
  } else if (vKind == KIND_TRIANGLE) {
    coverage = TriangleCoverage(vec3(dx, dFdx(vParams.y)), vec3(dy, dFdy(vParams.y)));
  } else if (vKind == KIND_ARC) {
    coverage = ArcCoverage(dx, dy);
  } else {
    coverage = RoundedRectCoverage(dx, dy);
  }
  FragColor = Premultiply(vec4(vColor.rgb, vColor.a * coverage));
}
