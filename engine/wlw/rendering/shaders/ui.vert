// The canvas (ui::Canvas): batched quads in canvas units.
layout (location = 0) in vec2 aPos;
layout (location = 1) in vec2 aUV;
layout (location = 2) in vec4 aColor;
layout (location = 3) in vec4 aParams;  // x: the quad's kind (ui.frag); the rest depends on it

uniform mat4 uProjection;  // canvas units -> clip space

out vec2 vUV;
out vec4 vColor;
out vec4 vParams;

void main() {
  gl_Position = uProjection * vec4(aPos, 0.0, 1.0);
  vUV = aUV;
  vColor = aColor;
  vParams = aParams;
}
