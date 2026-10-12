// What a mesh shader's vertex() gets (see rendering/shader.h). Put in front of the game's text.
// The frame block gives uView, uProjection, uViewProjection, uCameraPos, the lights...
#include "wlw/frame.glsl"

layout (location = 0) in vec3 aPos;
layout (location = 1) in vec4 aColor;
layout (location = 2) in vec3 aNormal;
layout (location = 3) in vec2 aUV;
layout (location = 4) in mat4 aInstanceTransform;
layout (location = 8) in vec4 aInstanceTint;

uniform mat4 uModel;
uniform bool uInstanced;

out vec3 vWorldPos;
out vec3 vNormal;
out vec2 vUV;
out vec4 vColor;
