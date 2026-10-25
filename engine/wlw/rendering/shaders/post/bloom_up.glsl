// A bloom level going back up (rendering/post_settings.h: BloomSettings): the level below it
// (uSource, half this size, already built up) blurred with a 3 x 3 tent filter while stretched to
// this size, mixed with this level's own down picture (uLevel). Scatter = how much of the wider
// glow from below is kept (Unity URP's bloom: lerp(high mip, low mip, scatter)).
shader_type post;

uniform sampler2D uLevel;  // this level's picture from the way down
uniform float uScatter;    // 0..1

void fragment() {
  vec2 t = uTexel;  // the smaller level's texel
  vec3 sum = texture(uSource, vUV).rgb * 4.0;
  sum += (texture(uSource, vUV + vec2(-t.x, 0.0)).rgb + texture(uSource, vUV + vec2(t.x, 0.0)).rgb +
          texture(uSource, vUV + vec2(0.0, -t.y)).rgb + texture(uSource, vUV + vec2(0.0, t.y)).rgb) * 2.0;
  sum += texture(uSource, vUV + vec2(-t.x, -t.y)).rgb + texture(uSource, vUV + vec2(t.x, -t.y)).rgb +
         texture(uSource, vUV + vec2(-t.x, t.y)).rgb + texture(uSource, vUV + vec2(t.x, t.y)).rgb;
  vec3 below = sum / 16.0;
  FragColor = vec4(mix(texture(uLevel, vUV).rgb, below, uScatter), 1.0);
}
