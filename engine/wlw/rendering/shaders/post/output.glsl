// The output pass (rendering/post_settings.h): the frame's last full-screen pass, from the HDR
// picture to the 8-bit destination in one read and one write. Exposure, then the tone mapper,
// then dithering. The renderer sets the params from the viewport's PostSettings.
shader_type post;

uniform float uExposure;  // the picture is multiplied by it first
uniform float uShoulder;  // 1 = ToneMapper::Shoulder, 0 = Clamp
uniform float uDither;    // 1 = +-half an 8-bit step of noise

// Khronos PBR Neutral's shoulder (github.com/KhronosGroup/ToneMapping), without its toe: below
// kStart nothing changes; above, the brightest channel rolls off towards 1 (1.0 shows as 0.9,
// 2.0 as 0.97) and the colour is scaled with it, so the hue stays; very bright colours then fade
// towards white (kDesaturation), as real highlights do.
vec3 Shoulder(vec3 color) {
  const float kStart = 0.8;
  const float kDesaturation = 0.15;
  float peak = max(color.r, max(color.g, color.b));
  if (peak < kStart) return color;
  const float d = 1.0 - kStart;
  float new_peak = 1.0 - d * d / (peak + d - kStart);
  color *= new_peak / peak;
  float g = 1.0 - 1.0 / (kDesaturation * (peak - new_peak) + 1.0);
  return mix(color, vec3(new_peak), g);
}

// Interleaved gradient noise (Jimenez, "Next Generation Post Processing in Call of Duty: AW"):
// 0..1, evenly spread between neighbouring pixels, no texture needed.
float GradientNoise(vec2 pixel) { return fract(52.9829189 * fract(dot(pixel, vec2(0.06711056, 0.00583715)))); }

void fragment() {
  vec4 color = texture(uSource, vUV);
  vec3 rgb = max(color.rgb * uExposure, vec3(0.0));
  if (uShoulder > 0.5) rgb = Shoulder(rgb);
  if (uDither > 0.5) rgb += (GradientNoise(gl_FragCoord.xy) - 0.5) / 255.0;
  FragColor = vec4(clamp(rgb, 0.0, 1.0), clamp(color.a, 0.0, 1.0));
}
