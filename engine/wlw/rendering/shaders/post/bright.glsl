// Bloom's first pass: only what is brighter than the threshold, with a soft knee so the glow
// fades in instead of switching on (the curve Unity's and Unreal's bloom use).
shader_type post;

uniform float uThreshold;  // brightness (the largest of r, g, b) where glow starts
uniform float uKnee;       // how far below the threshold it starts fading in (0 = a hard cut)

void fragment() {
  vec4 color = texture(uSource, vUV);
  float brightness = max(color.r, max(color.g, color.b));
  float soft = clamp(brightness - uThreshold + uKnee, 0.0, 2.0 * uKnee);
  soft = soft * soft / (4.0 * uKnee + 1e-4);
  float glow = max(soft, brightness - uThreshold) / max(brightness, 1e-4);
  FragColor = color * glow;
}
