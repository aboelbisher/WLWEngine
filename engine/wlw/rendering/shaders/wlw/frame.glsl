// The per-frame uniform block: the camera, the lights and the shadow, uploaded once per viewport
// and read by every program (engine shaders and games' mesh shaders). Its C++ twin, with the same
// std140 layout, is rendering/frame_uniforms.h: change both together.

#define WLW_MAX_LIGHTS_PER_TYPE 4

struct WlwDirectionalLight {
  vec4 direction_ambient;  // xyz: where it shines (world, normalized); w: ambient
  vec4 color;              // rgb: colour x intensity
};

struct WlwPointLight {
  vec4 position_ambient;   // xyz: world position; w: ambient
  vec4 color;              // rgb: colour x intensity
  vec4 attenuation;        // xyz: constant, linear, quadratic falloff
};

struct WlwSpotLight {
  vec4 position_ambient;   // xyz: world position; w: ambient
  vec4 direction;          // xyz: where it shines (world, normalized)
  vec4 color;              // rgb: colour x intensity
  vec4 attenuation;        // xyz: constant, linear, quadratic falloff
  vec4 cone;               // x: cos(inner angle), y: cos(outer angle)
};

layout(std140) uniform WlwFrame {
  mat4 uView;
  mat4 uProjection;
  mat4 uViewProjection;
  mat4 uLightSpace;         // the shadow light's view-projection
  vec3 uCameraPos;
  float uOrthographic;      // 1: orthographic camera (parallel view rays), 0: perspective
  vec3 uViewForward;        // the camera's forward axis (world)
  float uShadowStrength;    // 0 = shadows don't darken, 1 = only ambient is left in shadow
  vec4 uShadowParams;       // x: filter radius in texels, y: 1 / map size, z: normal offset (world)
  ivec4 uLightCounts;       // directional, point, spot lights; w: the shadow light (a directional index, -1 = none)
  WlwDirectionalLight uDirectionalLights[WLW_MAX_LIGHTS_PER_TYPE];
  WlwPointLight uPointLights[WLW_MAX_LIGHTS_PER_TYPE];
  WlwSpotLight uSpotLights[WLW_MAX_LIGHTS_PER_TYPE];
};
