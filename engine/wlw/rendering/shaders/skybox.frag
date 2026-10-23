in highp vec3 vDirection;

uniform mediump samplerCube uSkybox;  // texture unit 0

out vec4 FragColor;

void main() {
  // World (Z up) to the cube's Y-up layout (core/math.h ToYUp): the +Y face is the sky above, -Z ahead.
  FragColor = texture(uSkybox, vec3(vDirection.x, vDirection.z, -vDirection.y));
}
