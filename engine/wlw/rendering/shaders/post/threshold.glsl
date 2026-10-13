// Turns a density picture into solid shapes: metaballs, liquid. Dots drawn Additive add up their
// alpha (density) and their colour x density; blurred, that's a smooth field. Where the density
// passes the threshold the shape is solid, coloured by the dots' average colour there (rgb / a).
// Outside it is see-through (alpha 0), so the picture can be laid over anything.
// Best from an RGBA16F viewport: RGBA8 clamps sums at 1, which skews mixed colours.
shader_type post;

uniform float uThreshold;  // density where the surface is (0..1 in RGBA8)
uniform float uSoftness;   // half the width of the edge's fade, in density (anti-aliasing)

void fragment() {
  vec4 field = texture(uSource, vUV);
  float density = field.a;
  float inside = smoothstep(uThreshold - uSoftness, uThreshold + uSoftness, density);
  vec3 color = field.rgb / max(density, 1e-4);
  FragColor = vec4(color * inside, inside);  // premultiplied
}
