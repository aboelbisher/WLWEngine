// Lighting with the frame's lights (wlw/frame.glsl): Blinn-Phong, split by light type so no loop
// branches on the type. Include after wlw/frame.glsl.

// Direction from a surface point towards the eye. Orthographic cameras: the same everywhere.
vec3 ViewDirection(vec3 position) {
  return uOrthographic > 0.5 ? -uViewForward : normalize(uCameraPos - position);
}

// One light at a surface: its ambient, plus diffuse + specular scaled by `direct` (a spot's cone,
// a shadow). `to_light` and `normal` normalized.
vec3 BlinnPhong(vec3 to_light, vec3 color, float ambient, float direct, vec3 normal, vec3 view_dir, float shininess) {
  vec3 halfway = normalize(to_light + view_dir);
  float diffuse = max(dot(normal, to_light), 0.0);
  float specular = diffuse > 0.0 ? pow(max(dot(normal, halfway), 0.0), shininess) : 0.0;
  return (ambient + (diffuse + specular) * direct) * color;
}

// The classic constant / linear / quadratic falloff with distance.
float Attenuation(vec3 k, float distance) {
  return 1.0 / (k.x + k.y * distance + k.z * distance * distance);
}

// The light arriving at a surface point from all of the frame's lights (multiply the surface
// colour by it). `shadow` (1 = lit, from ShadowVisibility) dims the shadow light's direct part.
vec3 SceneLighting(vec3 position, vec3 normal, vec3 view_dir, float shininess, float shadow) {
  vec3 light = vec3(0.0);
  for (int i = 0; i < uLightCounts.x; ++i) {
    WlwDirectionalLight l = uDirectionalLights[i];
    float direct = i == uLightCounts.w ? shadow : 1.0;
    light += BlinnPhong(-l.direction_ambient.xyz, l.color.rgb, l.direction_ambient.w, direct, normal, view_dir, shininess);
  }
  for (int i = 0; i < uLightCounts.y; ++i) {
    WlwPointLight l = uPointLights[i];
    vec3 offset = l.position_ambient.xyz - position;
    float distance = length(offset);
    vec3 to_light = offset / max(distance, 0.0001);
    light += BlinnPhong(to_light, l.color.rgb, l.position_ambient.w, 1.0, normal, view_dir, shininess) *
             Attenuation(l.attenuation.xyz, distance);
  }
  for (int i = 0; i < uLightCounts.z; ++i) {
    WlwSpotLight l = uSpotLights[i];
    vec3 offset = l.position_ambient.xyz - position;
    float distance = length(offset);
    vec3 to_light = offset / max(distance, 0.0001);
    // Full inside the inner cone, fading to nothing at the outer one.
    float theta = dot(to_light, -l.direction.xyz);
    float cone = clamp((theta - l.cone.y) / max(l.cone.x - l.cone.y, 0.0001), 0.0, 1.0);
    light += BlinnPhong(to_light, l.color.rgb, l.position_ambient.w, cone, normal, view_dir, shininess) *
             Attenuation(l.attenuation.xyz, distance);
  }
  return light;
}
