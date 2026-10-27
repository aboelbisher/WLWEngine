// The output pass (rendering/post_settings.h): the frame's last full-screen pass, from the HDR
// picture to the 8-bit destination in one read and one write. The picture is read where the
// distortion buffer, the ripples and chromatic aberration move it, the bloom's glow is added, then
// exposure, the vignette, the tone mapper and dithering. The renderer sets the params from the
// viewport's PostSettings.
//
// Compiled in variants (Shader::Create's defines), picked per frame:
//   DISTORT            ripples or chromatic aberration (the settings)
//   DISTORTION_BUFFER  something drew into the distortion buffer this frame
//   BLOOM              the glow is added
// Without them the pass reads the picture once, where it is.
shader_type post;

uniform float uExposure;  // the picture is multiplied by it (after the glow is added)
uniform float uShoulder;  // 1 = ToneMapper::Shoulder, 0 = Clamp
uniform float uDither;    // 1 = +-half an 8-bit step of noise
uniform vec4 uVignette;   // centre x, y (in vUV), intensity (0 = off), smoothness
uniform vec4 uVignetteColor;
#ifdef BLOOM
uniform sampler2D uBloom;       // the glow (the bloom's level 0, built up)
uniform float uBloomIntensity;
#endif
#ifdef DISTORT
#define MAX_RIPPLES 4                 // rendering::kMaxRipples
uniform vec4 uRings[MAX_RIPPLES];     // per ring: centre x, y (in vUV), radius, width (viewport heights)
uniform vec4 uRingPush[MAX_RIPPLES];  // per ring: the largest push (0 = off), the colour split (a fraction of it)
uniform float uChroma;                // chromatic aberration: the split at the corners (viewport heights)
#endif
#ifdef DISTORTION_BUFFER
uniform highp sampler2D uDistortion;  // per pixel: how far the picture there moved (viewport fractions, RG16F)
uniform vec2 uDistortionPush;         // the buffer's strength, its colour split
#endif

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
  vec2 uv = vUV;
#if defined(DISTORT) || defined(DISTORTION_BUFFER)
  vec2 shift = vec2(0.0);  // where this pixel reads from, relative to it (in vUV)
  vec2 split = vec2(0.0);  // red reads at shift + split, blue at shift - split (in vUV)
#ifdef DISTORT
  // Distances in viewport heights (x scaled by the aspect): rings stay round.
  float aspect = uOutputSize.x / uOutputSize.y;
  vec2 to_uv = vec2(1.0 / aspect, 1.0);
  vec2 ring_shift = vec2(0.0);
  vec2 ring_split = vec2(0.0);
  for (int i = 0; i < MAX_RIPPLES; ++i) {
    if (uRingPush[i].x == 0.0) continue;
    vec4 ring = uRings[i];
    vec2 d = (uv - ring.xy) * vec2(aspect, 1.0);
    float dist = length(d);
    float x = (dist - ring.z) / (0.5 * ring.w);  // -1..1 across the ring
    if (abs(x) >= 1.0 || dist == 0.0) continue;
    float k = 1.0 - x * x;
    // Reading from further in pushes the picture out; k^2 eases in and out with no kink at the edges.
    vec2 push = d * (uRingPush[i].x * k * k / dist);
    ring_shift -= push;
    ring_split -= push * uRingPush[i].y;
  }
  if (uChroma != 0.0) {
    vec2 r = (uv - 0.5) * vec2(aspect, 1.0);
    float corner2 = 0.25 * (aspect * aspect + 1.0);  // the squared distance to a corner
    ring_split += r * (length(r) * uChroma / corner2);
  }
  shift += ring_shift * to_uv;
  split += ring_split * to_uv;
#endif
#ifdef DISTORTION_BUFFER
  // The picture here moved by this much: read from where it came from.
  vec2 moved = texture(uDistortion, vUV).rg * uDistortionPush.x;
  shift -= moved;
  split -= moved * uDistortionPush.y;
#endif
  uv += shift;
  vec4 color = texture(uSource, uv);
  if (split != vec2(0.0)) {
    color.r = texture(uSource, uv + split).r;
    color.b = texture(uSource, uv - split).b;
  }
#else
  vec4 color = texture(uSource, uv);
#endif
  vec3 rgb = color.rgb;
#ifdef BLOOM
  rgb += texture(uBloom, uv).rgb * uBloomIntensity;
#endif
  rgb = max(rgb * uExposure, vec3(0.0));
  if (uVignette.z > 0.0) {
    // 0 at the centre, 1 at the corners of a centred one (an ellipse of the viewport's shape).
    float d = length((vUV - uVignette.xy) * 2.0) * 0.70710678;
    rgb = mix(rgb, uVignetteColor.rgb, uVignette.z * smoothstep(1.0 - uVignette.w, 1.0, d));
  }
  if (uShoulder > 0.5) rgb = Shoulder(rgb);
  if (uDither > 0.5) rgb += (GradientNoise(gl_FragCoord.xy) - 0.5) / 255.0;
  FragColor = vec4(clamp(rgb, 0.0, 1.0), clamp(color.a, 0.0, 1.0));
}
