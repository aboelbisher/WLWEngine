// An instanced draw's per-copy inputs (glVertexAttribDivisor 1: they advance once per copy), and the
// transforms built from them. C++ twin: rendering::Instance (rendering/instance_buffer.h), 52 bytes:
// the top 3 rows of the copy's affine transform (relative to the node; .w = translation) + an RGBA8
// tint the GPU scales to 0..1.

layout (location = 4) in highp vec4 aInstanceRow0;
layout (location = 5) in highp vec4 aInstanceRow1;
layout (location = 6) in highp vec4 aInstanceRow2;
layout (location = 7) in mediump vec4 aInstanceTint;

// The copy's transform as a 4x4 (bottom row 0 0 0 1).
highp mat4 InstanceTransform() {
  return transpose(mat4(aInstanceRow0, aInstanceRow1, aInstanceRow2, vec4(0.0, 0.0, 0.0, 1.0)));
}

// A point of the mesh moved by the copy's transform (3 dot products).
highp vec3 InstancePosition(highp vec3 local) {
  highp vec4 p = vec4(local, 1.0);
  return vec3(dot(aInstanceRow0, p), dot(aInstanceRow1, p), dot(aInstanceRow2, p));
}

// A mesh normal carried through the copy's transform: its cofactor matrix (see NormalMatrix in
// wlw/common.glsl), built from the rows. For a matrix with rows r0, r1, r2 the cofactor matrix's
// rows are r1 x r2, r2 x r0, r0 x r1; flipped when the copy mirrors (det = r0 . (r1 x r2) < 0).
// Not unit length.
highp vec3 InstanceNormal(highp vec3 normal) {
  highp vec3 r0 = aInstanceRow0.xyz;
  highp vec3 r1 = aInstanceRow1.xyz;
  highp vec3 r2 = aInstanceRow2.xyz;
  highp vec3 x = cross(r1, r2);
  highp float flip = dot(r0, x) < 0.0 ? -1.0 : 1.0;  // not sign(): det 0 must keep the normal
  return vec3(dot(x, normal), dot(cross(r2, r0), normal), dot(cross(r0, r1), normal)) * flip;
}
