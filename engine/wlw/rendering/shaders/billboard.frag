// A BillboardBatch's quad (variants: see billboard.vert). Output: premultiplied alpha, like every shader, so the same
// quad works Additive (light) and Alpha. The colour may pass 1 (intensity): an HDR picture keeps it for bloom.
in mediump vec2 vCorner;
in mediump vec4 vColor;

#ifdef TEXTURED
uniform mediump sampler2D uTexture;  // texture unit 0: its alpha (x rgb) shapes the quad
#endif

out vec4 FragColor;

void main() {
#ifdef TEXTURED
  // uv (0, 0) = the image's top row at the quad's top-left (corner y = +1 is up on screen).
  vec4 shape = texture(uTexture, vec2(0.5 + 0.5 * vCorner.x, 0.5 - 0.5 * vCorner.y));
  float alpha = vColor.a * shape.a;
  vec3 color = vColor.rgb * shape.rgb;
#else
  // The soft dot of utils::RadialFalloffTexture, (1 - d²)², without a texture read.
  float falloff = max(1.0 - dot(vCorner, vCorner), 0.0);
  float alpha = vColor.a * falloff * falloff;
  vec3 color = vColor.rgb;
#endif
  FragColor = vec4(color * alpha, alpha);
}
