// A SpriteBatch's dots (scene/sprite_batch.h): one instance per dot, 4 vertices each (a triangle
// strip). No vertex buffer: the corner comes from gl_VertexID, the rest from the dot's 16 bytes.
// Variant: TEXTURED (the dot's shape is the material's texture, else a computed soft falloff).
#include "wlw/frame.glsl"

layout (location = 4) in vec3 aSprite;       // per dot: x, y (node units, on its z = 0 plane), size
layout (location = 5) in vec4 aSpriteColor;  // per dot: RGBA8 read as 0..1

uniform mat4 uModel;

out vec2 vCorner;  // -1..1 across the dot
out vec4 vColor;

void main() {
  // Strip order: (-1,-1), (1,-1), (-1,1), (1,1).
  vec2 corner = vec2(float(gl_VertexID & 1), float(gl_VertexID >> 1)) * 2.0 - 1.0;
  vCorner = corner;
  vColor = aSpriteColor;
  vec2 position = aSprite.xy + corner * (0.5 * aSprite.z);
  gl_Position = uViewProjection * (uModel * vec4(position, 0.0, 1.0));
}
