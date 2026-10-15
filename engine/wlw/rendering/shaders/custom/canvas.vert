// Canvas shaders (Canvas::ShaderRect): the vertex stage is always this one. The rectangle's uv
// is 0..1, (0, 0) = top left.
layout (location = 0) in vec2 aPos;
layout (location = 1) in vec2 aUV;
layout (location = 2) in vec4 aColor;

uniform highp mat4 uProjection;

out highp vec2 vUV;
out mediump vec4 vColor;

void main() {
  gl_Position = uProjection * vec4(aPos, 0.0, 1.0);
  vUV = aUV;
  vColor = aColor;
}
