in vec3 vDirection;

uniform samplerCube uSkybox;  // texture unit 0

out vec4 FragColor;

void main() {
  FragColor = texture(uSkybox, vDirection);
}
