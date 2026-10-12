// Colour helpers. Everything the engine draws is premultiplied alpha (see rendering/blend_mode.h).

// Straight alpha (rgb, a) -> premultiplied (rgb * a, a).
vec4 Premultiply(vec4 color) { return vec4(color.rgb * color.a, color.a); }

// Perceived brightness (Rec. 709 weights).
float Luminance(vec3 color) { return dot(color, vec3(0.2126, 0.7152, 0.0722)); }
