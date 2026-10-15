// The per-frame uniform block: the camera, the lights and the shadow, uploaded once per viewport
// and read by every program (engine shaders and games' mesh shaders). Its C++ twin, with the same
// std140 layout, is rendering/frame_uniforms.h: change both together.
// Every member names its precision: the block is declared in both stages, whose default float
// precisions may differ on GLES (a shader that opts in to mediump, rendering/glsl_header.h), and
// both must agree. Positions, matrices,
// cone cosines and the shadow parameters are highp; colours, directions and factors mediump.

#define WLW_MAX_LIGHTS_PER_TYPE 4

struct WlwDirectionalLight {
  mediump vec4 direction_ambient;  // xyz: where it shines (world, normalized); w: ambient
  mediump vec4 color;              // rgb: colour x intensity
};

struct WlwPointLight {
  highp vec4 position_ambient;  // xyz: world position; w: ambient
  mediump vec4 color;           // rgb: colour x intensity
  mediump vec4 attenuation;     // xyz: constant, linear, quadratic falloff
};

struct WlwSpotLight {
  highp vec4 position_ambient;  // xyz: world position; w: ambient
  highp vec4 direction;         // xyz: where it shines (world, normalized)
  mediump vec4 color;           // rgb: colour x intensity
  mediump vec4 attenuation;     // xyz: constant, linear, quadratic falloff
  highp vec4 cone;              // x: cos(inner angle), y: cos(outer angle)
};

layout(std140) uniform WlwFrame {
  highp mat4 uView;
  highp mat4 uProjection;
  highp mat4 uViewProjection;
  highp mat4 uLightSpace;           // the shadow light's view-projection
  highp vec3 uCameraPos;
  mediump float uOrthographic;      // 1: orthographic camera (parallel view rays), 0: perspective
  mediump vec3 uViewForward;        // the camera's forward axis (world)
  mediump float uShadowStrength;    // 0 = shadows don't darken, 1 = only ambient is left in shadow
  highp vec4 uShadowParams;         // x: filter radius in texels, y: 1 / map size, z: normal offset (world)
  highp ivec4 uLightCounts;         // directional, point, spot lights; w: the shadow light (a directional index, -1 = none)
  WlwDirectionalLight uDirectionalLights[WLW_MAX_LIGHTS_PER_TYPE];
  WlwPointLight uPointLights[WLW_MAX_LIGHTS_PER_TYPE];
  WlwSpotLight uSpotLights[WLW_MAX_LIGHTS_PER_TYPE];
};
