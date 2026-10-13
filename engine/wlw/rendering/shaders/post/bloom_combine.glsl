// Bloom's last pass: the picture from before the bloom + its blurred bright parts.
shader_type post;

uniform sampler2D uBase;  // the picture before the bloom
uniform float uStrength;  // how much glow is added

void fragment() {
  vec4 color = texture(uBase, vUV) + texture(uSource, vUV) * uStrength;
  FragColor = vec4(color.rgb, min(color.a, 1.0));
}
