// The sky: one triangle over the whole viewport (corners (-1,-1), (3,-1), (-1,3), no vertex buffer:
// drawn with glDrawArrays(GL_TRIANGLES, 0, 3)), at depth 1.0. Drawn after the opaque meshes with
// the depth test LEQUAL (rendering/sky.h): only the pixels nothing covers run the fragment stage.

uniform highp mat4 uSkyRay;  // clip space -> world view ray (SkyRayMatrix, without the view's translation)

out highp vec3 vDirection;

void main() {
  vec2 corner = vec2(float((gl_VertexID << 1) & 2), float(gl_VertexID & 2)) * 2.0 - 1.0;
  // z = w: depth exactly 1.0, the farthest possible. The rays interpolate linearly: exact on the far plane.
  gl_Position = vec4(corner, 1.0, 1.0);
  vDirection = (uSkyRay * gl_Position).xyz;
}
