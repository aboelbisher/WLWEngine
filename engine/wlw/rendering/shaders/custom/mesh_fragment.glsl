// What a mesh shader's fragment() gets (see rendering/shader.h). Put in front of the game's text.
// The frame block gives the camera and the lights; #include "wlw/lighting.glsl" to light the
// surface with SceneLighting().
#include "wlw/frame.glsl"

uniform mat4 uModel;
in vec3 vWorldPos;
in vec3 vNormal;      // world, not normalized
in vec2 vUV;
in vec4 vColor;
out vec4 FragColor;   // premultiplied alpha
