// A BillboardBatch's quad (variants: see billboard.vert). Output: premultiplied alpha, like every shader, so the same
// quad works Additive (light) and Alpha. The colour may pass 1 (intensity): an HDR picture keeps it for bloom.
in mediump vec2 vCorner;
in mediump vec4 vColor;
#ifdef DISTORTION
in highp vec4 vAxes;
#endif

#ifdef TEXTURED
uniform mediump sampler2D uTexture;  // texture unit 0: its alpha (x rgb) shapes the quad
#endif

out vec4 FragColor;

void main() {
#ifdef DISTORTION
  // A distortion quad (into the RG16F buffer, added up): how far the picture under this pixel moves, in viewport
  // fractions. The push's direction in the quad's own axes (x along, y across = up in its image), its size 0..1 of
  // the quad's half size, x the colour's alpha x intensity (vColor.a).
#ifdef TEXTURED
  // A direction map like a normal map's: red and green 0..1 are -1..1 (x right, y up in the image), alpha the size
  // (utils::RingDistortionTexture). Alpha 0 = no push, whatever the colour (the 8-bit 0.5 isn't exactly 0).
  vec4 shape = texture(uTexture, vec2(0.5 + 0.5 * vCorner.x, 0.5 - 0.5 * vCorner.y));
  vec2 direction = shape.rg * 2.0 - 1.0;
  float amount = shape.a;
#else
  // Without a texture: a lens, pushing outwards from the middle, the most halfway to the edge.
  vec2 direction = vCorner;
  float falloff = max(1.0 - dot(vCorner, vCorner), 0.0);
  float amount = falloff * falloff;
#endif
  vec2 push = (vAxes.xy * direction.x + vAxes.zw * direction.y) * (amount * vColor.a);
  FragColor = vec4(push, 0.0, 0.0);
#else
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
#endif
}
