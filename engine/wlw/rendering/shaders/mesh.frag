// The engine's 3D shader, fragment stage (variants: see mesh.vert). Output: premultiplied alpha.
#include "wlw/frame.glsl"
#ifdef LIT
#include "wlw/lighting.glsl"
#endif
#ifdef SHADOWS
#include "wlw/shadows.glsl"
#endif

in mediump vec4 vColor;
in mediump vec3 vNormal;
in highp vec3 vWorldPos;
in highp vec2 vUV;
#ifdef SHADOWS
in highp vec4 vLightSpacePos;
#endif

uniform highp float uShininess;  // specular highlight sharpness
#ifdef TEXTURED
uniform mediump sampler2D uTexture;  // texture unit 0; replaces the vertex colour (keeps its alpha)
#endif

out vec4 FragColor;

void main() {
#ifdef TEXTURED
  vec4 base = texture(uTexture, vUV) * vec4(1.0, 1.0, 1.0, vColor.a);
#else
  vec4 base = vColor;
#endif
  vec3 color = base.rgb;

#ifdef LIT
#ifdef SHADOWS
  float shadow = ShadowVisibility(vLightSpacePos);
#else
  float shadow = 1.0;
#endif
  color *= SceneLighting(vWorldPos, normalize(vNormal), ViewDirection(vWorldPos), uShininess, shadow);
#endif

#ifdef BLEND
  FragColor = vec4(color * base.a, base.a);
#else
  FragColor = vec4(color, 1.0);  // opaque: never leaves see-through pixels behind
#endif
}
