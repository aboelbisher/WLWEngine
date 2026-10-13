// One direction of a separable Gaussian blur (scene/post_effects.h: AddBlur runs it across, then
// down). A 9-tap Gaussian in 5 texture reads: each pair of taps beside the centre is one read
// between them, where linear filtering mixes the two in the right proportion (Rakos, "Efficient
// Gaussian blur with linear sampling"). The weights add up to 1.
shader_type post;

uniform vec2 uDirection;  // (1, 0) across, (0, 1) down
uniform float uSpread;    // 1 = the Gaussian's own spacing; more = a wider (sparser) blur

void fragment() {
  vec2 stride = uDirection * uTexel * uSpread;
  vec2 near = stride * 1.3846153846;
  vec2 far = stride * 3.2307692308;
  vec4 sum = texture(uSource, vUV) * 0.2270270270;
  sum += (texture(uSource, vUV + near) + texture(uSource, vUV - near)) * 0.3162162162;
  sum += (texture(uSource, vUV + far) + texture(uSource, vUV - far)) * 0.0702702703;
  FragColor = sum;
}
