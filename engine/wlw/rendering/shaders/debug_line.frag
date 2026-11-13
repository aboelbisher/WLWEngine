// Debug draw's lines (see debug_line.vert). Output: premultiplied alpha, like every engine shader.
in mediump vec4 vColor;
in highp float vAcross;
flat in highp float vHalfWidth;

uniform mediump float uAlpha;  // x the line's alpha: the faint pass of X-ray lines

out vec4 FragColor;

void main() {
  // Coverage across the line: whole inside, down to nothing over its outermost pixel (no MSAA needed).
  mediump float coverage = clamp(vHalfWidth + 0.5 - abs(vAcross), 0.0, 1.0);
  mediump float alpha = vColor.a * coverage * uAlpha;
  FragColor = vec4(vColor.rgb * alpha, alpha);
}
