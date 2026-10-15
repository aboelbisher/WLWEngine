// What a mesh shader's fragment() gets (see rendering/shader.h). Put in front of the game's text.
// The frame block gives the camera and the lights; #include "wlw/lighting.glsl" to light the
// surface with SceneLighting().
#include "wlw/frame.glsl"

uniform highp mat4 uModel;
uniform highp mat3 uNormalMatrix;  // carries the node's normals to world space (not unit length)
in highp vec3 vWorldPos;
in mediump vec3 vNormal;  // world; unit length at the vertices, not in between (normalize it)
in highp vec2 vUV;
in mediump vec4 vColor;
out vec4 FragColor;       // premultiplied alpha
// The game's text is highp unless it says `precision mediump float;` (rendering/shader.h).
precision highp float;
