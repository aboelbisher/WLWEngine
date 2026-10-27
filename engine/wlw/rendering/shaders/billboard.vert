// A BillboardBatch's quads (scene/billboard_batch.h): one instance per quad, 4 vertices each (a triangle strip). No
// vertex buffer: the corner comes from gl_VertexID, the rest from the quad's 40 bytes. The quad is built in VIEW space
// (x right, y up on screen), so it always faces the camera, perspective or orthographic.
// Variants: TEXTURED (the shape is the material's texture, else a computed soft falloff); DISTORTION (a distortion
// material, post_settings.h: the quad writes how far it moves the picture instead of colour).
#include "wlw/frame.glsl"

layout (location = 4) in vec4 aBillboard;  // per quad: xyz centre (node units), w size
layout (location = 5) in vec4 aStretch;    // per quad: xyz stretch (node units), w rotation (radians)
layout (location = 6) in vec4 aColor;      // per quad: RGBA8 read as 0..1
layout (location = 7) in float aIntensity; // per quad: multiplies rgb

uniform highp mat4 uModel;

out mediump vec2 vCorner;  // -1..1 across the quad (x along its length, y across)
out mediump vec4 vColor;   // rgb x intensity: may pass 1 (HDR)
#ifdef DISTORTION
out highp vec4 vAxes;  // the quad's half axes on screen, in viewport fractions: xy along its length, zw across
#endif

void main() {
  // Strip order: (-1,-1), (1,-1), (-1,1), (1,1).
  vec2 corner = vec2(float(gl_VertexID & 1), float(gl_VertexID >> 1)) * 2.0 - 1.0;
  vCorner = corner;
  vColor = vec4(aColor.rgb * aIntensity, aColor.a);

  highp mat4 model_view = uView * uModel;
  highp vec3 center = (model_view * vec4(aBillboard.xyz, 1.0)).xyz;
  highp float half_width = 0.5 * aBillboard.w * length(uModel[0].xyz);  // node scale (uniform assumed)
  // The stretch as seen on screen: its view-space x and y (the part along the view direction doesn't show).
  highp vec2 axis = (model_view * vec4(aStretch.xyz, 0.0)).xy;
  highp float axis_length = length(axis);
  highp vec2 along = vec2(cos(aStretch.w), sin(aStretch.w));  // round: turned by the rotation
  highp float half_length = half_width;
  if (axis_length > 1e-6) {
    along = axis / axis_length;
    half_length += 0.5 * axis_length;
  }
  highp vec2 across = vec2(-along.y, along.x);
  highp vec2 offset = along * (corner.x * half_length) + across * (corner.y * half_width);
  gl_Position = uProjection * vec4(center.xy + offset, center.z, 1.0);
#ifdef DISTORTION
  // Where the quad's centre and the ends of its half axes land on screen (perspective divides them differently).
  highp vec4 middle = uProjection * vec4(center, 1.0);
  highp vec4 end_along = uProjection * vec4(center.xy + along * half_length, center.z, 1.0);
  highp vec4 end_across = uProjection * vec4(center.xy + across * half_width, center.z, 1.0);
  highp vec2 ndc = middle.xy / middle.w;
  vAxes = vec4(end_along.xy / end_along.w - ndc, end_across.xy / end_across.w - ndc) * 0.5;  // NDC -> fractions
  vColor = vec4(aColor.rgb, aColor.a * aIntensity);  // a = how strongly it pushes
#endif
}
