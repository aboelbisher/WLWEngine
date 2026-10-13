// Post shaders (a viewport's post passes): the vertex stage is always this one. One triangle that
// covers the whole output (corners at (-1,-1), (3,-1), (-1,3): no diagonal seam, no vertex
// buffer: drawn with glDrawArrays(GL_TRIANGLES, 0, 3)). vUV is 0..1 across the output, (0, 0) =
// bottom left, the same orientation as texture coordinates.
out vec2 vUV;

void main() {
  vec2 corner = vec2(float((gl_VertexID << 1) & 2), float(gl_VertexID & 2));  // (0,0) (2,0) (0,2)
  vUV = corner;
  gl_Position = vec4(corner * 2.0 - 1.0, 0.0, 1.0);
}
