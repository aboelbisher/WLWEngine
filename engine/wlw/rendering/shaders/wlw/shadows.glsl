// Shadows from the frame's shadow light (one directional light, wlw/frame.glsl). Include after
// wlw/frame.glsl. The engine's shader computes where to look a point up per vertex and looks per
// fragment; a game's mesh shader calls SceneShadow() in fragment(), which does both.

// Where a world point is in the shadow map's space. Pushing it out along the normal keeps a
// surface from shadowing itself ("shadow acne": it compares against nearly its own depth).
highp vec4 ShadowLookupPosition(highp vec3 world_position, highp vec3 normal) {
  return uLightSpace * vec4(world_position + normal * uShadowParams.z, 1.0);
}

#ifdef WLW_FRAGMENT
uniform highp sampler2DShadow uShadowMap;  // texture unit 1

// How much of the shadow light reaches this point (1 = all), with the shadow strength applied.
// Each texture() on a sampler2DShadow compares our depth with the stored one (the GPU blends the
// 4 nearest results); 9 taps around the point average into a soft edge (percentage-closer filtering).
// The lookup position stays highp: shadow-map coordinates and depth need more than fp16 has.
float ShadowVisibility(highp vec4 light_space_position) {
  highp vec3 p = light_space_position.xyz / light_space_position.w * 0.5 + 0.5;
  if (p.x < 0.0 || p.x > 1.0 || p.y < 0.0 || p.y > 1.0 || p.z > 1.0) return 1.0;  // outside the map
  highp float radius = uShadowParams.x * uShadowParams.y;
  float lit = 0.0;
  for (int x = -1; x <= 1; ++x) {
    for (int y = -1; y <= 1; ++y) {
      lit += texture(uShadowMap, vec3(p.xy + vec2(x, y) * radius, p.z));
    }
  }
  return mix(1.0, lit / 9.0, uShadowStrength);
}

// Game mesh shaders: true when this draw is shadowed (the frame has a shadow light and the node
// receives shadows, GeometryInstance3D::SetReceivesShadow). Set by the renderer per draw.
uniform bool uReceivesShadow;

// The shadow at a surface point for a game's mesh shader (1 = lit), to pass to SceneLighting:
// 1 when the draw isn't shadowed. `normal` is the world normal (unit length).
float SceneShadow(highp vec3 world_position, highp vec3 normal) {
  if (!uReceivesShadow) return 1.0;
  return ShadowVisibility(ShadowLookupPosition(world_position, normal));
}
#endif
