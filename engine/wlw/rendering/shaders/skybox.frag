in highp vec3 vDirection;

uniform mediump samplerCube uSkybox;  // texture unit 0

out vec4 FragColor;

void main() {
  FragColor = texture(uSkybox, vDirection);
}
