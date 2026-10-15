// A SpriteBatch's dot (variants: see sprite.vert). Output: premultiplied alpha, like every shader,
// so the same dot works Additive (glow, density for metaballs) and Alpha.
in mediump vec2 vCorner;
in mediump vec4 vColor;

#ifdef TEXTURED
uniform mediump sampler2D uTexture;  // texture unit 0: its alpha (x rgb) shapes the dot
#endif

out vec4 FragColor;

void main() {
#ifdef TEXTURED
  vec4 shape = texture(uTexture, vCorner * 0.5 + 0.5);
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
