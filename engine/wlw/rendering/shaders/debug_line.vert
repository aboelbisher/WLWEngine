// Debug draw's lines (debug/debug_draw.h): one instance per line, 4 vertices each (a triangle strip). No vertex
// buffer: the corner comes from gl_VertexID, the rest from the line's 32 bytes. The line is cut at the near plane (one
// passing behind the camera keeps its visible part), then widened on screen in pixels, with square caps (half the
// width past each end, so box corners close) and one more pixel each side for a soft edge.
#include "wlw/frame.glsl"

layout (location = 4) in vec3 aStart;   // per line: world
layout (location = 5) in vec3 aEnd;     // per line: world
layout (location = 6) in vec4 aColor;   // per line: RGBA8 read as 0..1, straight alpha
layout (location = 7) in float aWidth;  // per line: dp

uniform highp vec2 uViewportPixels;  // the size drawn into
uniform highp float uWidthScale;     // pixels per dp
uniform highp float uDepthBias;      // the fraction of its distance a line moves towards the camera

out mediump vec4 vColor;            // straight alpha
out highp float vAcross;            // pixels from the line's middle, signed
flat out highp float vHalfWidth;    // half its width in pixels

// Into clip space, pulled towards the camera a little so a line lying on a surface isn't hidden by it: along its ray
// (perspective: the same point on screen) or straight towards the camera (orthographic).
highp vec4 ToClip(highp vec3 world) {
  highp vec4 view = uView * vec4(world, 1.0);
  view.xyz *= uOrthographic > 0.5 ? vec3(1.0, 1.0, 1.0 - uDepthBias) : vec3(1.0 - uDepthBias);
  return uProjection * view;
}

void main() {
  // Strip order: (start, -1), (end, -1), (start, +1), (end, +1).
  bool at_end = (gl_VertexID & 1) == 1;
  highp float side = float(gl_VertexID >> 1) * 2.0 - 1.0;

  highp vec4 a = ToClip(aStart);
  highp vec4 b = ToClip(aEnd);
  // The near plane is clip z = -w: z + w < 0 is in front of it (behind the camera, for a perspective one).
  highp float da = a.z + a.w;
  highp float db = b.z + b.w;
  if (da < 0.0 && db < 0.0) {
    gl_Position = vec4(2.0, 2.0, 2.0, 1.0);  // wholly cut away: every corner at one point outside the picture
    vColor = vec4(0.0);
    vAcross = 0.0;
    vHalfWidth = 0.0;
    return;
  }
  highp vec4 cut_a = da < 0.0 ? mix(a, b, da / (da - db)) : a;
  highp vec4 cut_b = db < 0.0 ? mix(b, a, db / (db - da)) : b;

  // On screen, in pixels from the middle of the picture.
  highp vec2 half_pixels = 0.5 * uViewportPixels;
  highp vec2 screen_a = cut_a.xy / cut_a.w * half_pixels;
  highp vec2 screen_b = cut_b.xy / cut_b.w * half_pixels;
  highp vec2 along = screen_b - screen_a;
  highp float length_pixels = length(along);
  along = length_pixels > 1e-4 ? along / length_pixels : vec2(1.0, 0.0);
  highp vec2 across = vec2(-along.y, along.x);

  highp float width = aWidth * uWidthScale;
  highp float half_width = 0.5 * max(width, 1.0);  // thinner ones are drawn 1 pixel wide and fainter
  highp float reach = half_width + 1.0;
  highp vec4 clip = at_end ? cut_b : cut_a;
  highp vec2 corner = (at_end ? screen_b + along * half_width : screen_a - along * half_width) + across * (side * reach);
  gl_Position = vec4(corner / half_pixels * clip.w, clip.z, clip.w);

  vAcross = side * reach;
  vHalfWidth = half_width;
  vColor = vec4(aColor.rgb, aColor.a * clamp(width, 0.0, 1.0));
}
