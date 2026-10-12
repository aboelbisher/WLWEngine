// A cube around the camera, textured with a cubemap; drawn first, behind everything.
#include "wlw/frame.glsl"

layout (location = 0) in vec3 aPos;

out vec3 vDirection;

void main() {
  vDirection = aPos;
  // The view without its translation: the sky stays centred on the camera.
  vec4 position = uProjection * mat4(mat3(uView)) * vec4(aPos, 1.0);
  gl_Position = position.xyww;  // depth 1.0: the farthest possible
}
