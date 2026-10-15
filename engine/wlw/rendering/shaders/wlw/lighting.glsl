// Lighting with the frame's lights (wlw/frame.glsl): Blinn-Phong, split by light type so no loop
// branches on the type. Include after wlw/frame.glsl.
// Precision (rendering/glsl_header.h): positions, offsets, distances, attenuation, the spot cone and
// the specular term are named highp, so the lighting stays right in a shader that opts in to
// mediump; the rest of the light maths (colours, normals, directions) follows the shader's default.

// Direction from a surface point towards the eye. Orthographic cameras: the same everywhere.
vec3 ViewDirection(highp vec3 position) {
  return uOrthographic > 0.5 ? -uViewForward : normalize(uCameraPos - position);
}

// One light at a surface: its ambient, plus diffuse + specular scaled by `direct` (a spot's cone,
// a shadow). `to_light` and `normal` normalized.
vec3 BlinnPhong(vec3 to_light, vec3 color, float ambient, float direct, vec3 normal, vec3 view_dir,
                highp float shininess) {
  float diffuse = max(dot(normal, to_light), 0.0);
  // The specular term in highp: in fp16, N.H within 0.00024 of 1 rounds to exactly 1, and pow()
  // of a coarse N.H draws the highlight as rings (shininess 64: ~22 bands down to half brightness).
  highp vec3 n = normal;
  highp vec3 l = to_light;
  highp vec3 v = view_dir;
  highp vec3 halfway = normalize(l + v);
  highp float specular = diffuse > 0.0 ? pow(max(dot(n, halfway), 0.0), shininess) : 0.0;
  return (ambient + (diffuse + specular) * direct) * color;
}

// The classic constant / linear / quadratic falloff with distance.
highp float Attenuation(highp vec3 k, highp float distance) {
  return 1.0 / (k.x + k.y * distance + k.z * distance * distance);
}

// The light arriving at a surface point from all of the frame's lights (multiply the surface
// colour by it). `shadow` (1 = lit, from ShadowVisibility) dims the shadow light's direct part.
vec3 SceneLighting(highp vec3 position, vec3 normal, vec3 view_dir, highp float shininess, float shadow) {
  vec3 light = vec3(0.0);
  for (int i = 0; i < uLightCounts.x; ++i) {
    WlwDirectionalLight l = uDirectionalLights[i];
    float direct = i == uLightCounts.w ? shadow : 1.0;
    light += BlinnPhong(-l.direction_ambient.xyz, l.color.rgb, l.direction_ambient.w, direct, normal, view_dir, shininess);
  }
  for (int i = 0; i < uLightCounts.y; ++i) {
    WlwPointLight l = uPointLights[i];
    highp vec3 offset = l.position_ambient.xyz - position;
    highp float distance = length(offset);
    vec3 to_light = offset / max(distance, 0.0001);
    light += BlinnPhong(to_light, l.color.rgb, l.position_ambient.w, 1.0, normal, view_dir, shininess) *
             Attenuation(l.attenuation.xyz, distance);
  }
  for (int i = 0; i < uLightCounts.z; ++i) {
    WlwSpotLight l = uSpotLights[i];
    highp vec3 offset = l.position_ambient.xyz - position;
    highp float distance = length(offset);
    highp vec3 to_light = offset / max(distance, 0.0001);
    // Full inside the inner cone, fading to nothing at the outer one. Narrow cones have cosines
    // near 1, where fp16's steps are 0.0005: theta stays highp.
    highp float theta = dot(to_light, -l.direction.xyz);
    float cone = clamp((theta - l.cone.y) / max(l.cone.x - l.cone.y, 0.0001), 0.0, 1.0);
    light += BlinnPhong(to_light, l.color.rgb, l.position_ambient.w, cone, normal, view_dir, shininess) *
             Attenuation(l.attenuation.xyz, distance);
  }
  return light;
}
