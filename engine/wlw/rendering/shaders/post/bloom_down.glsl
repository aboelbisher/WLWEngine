// A bloom level going down (rendering/post_settings.h: BloomSettings): half the size of what it
// reads, with Jimenez's 13-tap filter ("Next Generation Post Processing in Call of Duty: Advanced
// Warfare"): 13 bilinear reads covering 6 x 6 source texels, as five overlapping 2 x 2 boxes (the
// centre one weighs 0.5, the four corner ones 0.125 each). Every source texel counts, so a
// 1-pixel spark is never skipped and doesn't flicker as it moves (a single bilinear read per
// output texel only sees 4 of the 16 texels a quarter-size level covers).
// The first level also keeps only what is above the threshold (per tap) and weighs each box by
// 1 / (1 + its brightness) (Karis's average): one very bright texel then can't make a whole box
// blink ("fireflies"). The renderer sets the params for each level.
shader_type post;

uniform float uFirst;      // 1 on level 0 (reads the picture), else 0
uniform float uThreshold;  // brightness (the largest of r, g, b) where glow starts
uniform float uKnee;       // how far below the threshold it fades in

// The soft-knee threshold Unity's and Unreal's bloom use: nothing below threshold - knee, a
// smooth curve up to threshold, then everything above it.
vec3 Bright(vec3 color) {
  float brightness = max(color.r, max(color.g, color.b));
  float soft = clamp(brightness - uThreshold + uKnee, 0.0, 2.0 * uKnee);
  soft = soft * soft / (4.0 * uKnee + 1e-4);
  return color * (max(soft, brightness - uThreshold) / max(brightness, 1e-4));
}

vec3 Tap(vec2 uv) {
  vec3 color = max(texture(uSource, uv).rgb, vec3(0.0));
  return uFirst > 0.5 ? Bright(color) : color;
}

float KarisWeight(vec3 box) { return 1.0 / (1.0 + max(box.r, max(box.g, box.b))); }

void fragment() {
  // Half an output texel: the source's texel at an exact halving (wider steps for a first level
  // smaller than half the picture, so the taps still cover it).
  vec2 t = 0.5 / uOutputSize;
  vec3 a = Tap(vUV + t * vec2(-2.0, -2.0));
  vec3 b = Tap(vUV + t * vec2(0.0, -2.0));
  vec3 c = Tap(vUV + t * vec2(2.0, -2.0));
  vec3 d = Tap(vUV + t * vec2(-1.0, -1.0));
  vec3 e = Tap(vUV + t * vec2(1.0, -1.0));
  vec3 f = Tap(vUV + t * vec2(-2.0, 0.0));
  vec3 g = Tap(vUV);
  vec3 h = Tap(vUV + t * vec2(2.0, 0.0));
  vec3 i = Tap(vUV + t * vec2(-1.0, 1.0));
  vec3 j = Tap(vUV + t * vec2(1.0, 1.0));
  vec3 k = Tap(vUV + t * vec2(-2.0, 2.0));
  vec3 l = Tap(vUV + t * vec2(0.0, 2.0));
  vec3 m = Tap(vUV + t * vec2(2.0, 2.0));

  vec3 centre = (d + e + i + j) * 0.25;
  vec3 top_left = (a + b + f + g) * 0.25;
  vec3 top_right = (b + c + g + h) * 0.25;
  vec3 bottom_left = (f + g + k + l) * 0.25;
  vec3 bottom_right = (g + h + l + m) * 0.25;
  vec4 w = vec4(0.125);
  float wc = 0.5;
  if (uFirst > 0.5) {
    wc *= KarisWeight(centre);
    w *= vec4(KarisWeight(top_left), KarisWeight(top_right), KarisWeight(bottom_left), KarisWeight(bottom_right));
  }
  vec3 sum = centre * wc + top_left * w.x + top_right * w.y + bottom_left * w.z + bottom_right * w.w;
  FragColor = vec4(sum / (wc + w.x + w.y + w.z + w.w), 1.0);
}
