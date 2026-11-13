// Debug draw's triangles (see debug_triangle.vert). Output: premultiplied alpha, like every engine shader.
in mediump vec4 vColor;

uniform mediump float uAlpha;  // x the triangle's alpha: the faint pass of X-ray triangles

out vec4 FragColor;

void main() {
  mediump float alpha = vColor.a * uAlpha;
  FragColor = vec4(vColor.rgb * alpha, alpha);
}
