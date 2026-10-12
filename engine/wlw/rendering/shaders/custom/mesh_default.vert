// The vertex stage of a mesh shader without its own vertex(): the usual transform.
void main() {
  mat4 world = uInstanced ? uModel * aInstanceTransform : uModel;
  vec4 world_pos = world * vec4(aPos, 1.0);
  gl_Position = uViewProjection * world_pos;
  vWorldPos = world_pos.xyz;
  vNormal = mat3(transpose(inverse(world))) * aNormal;
  vUV = aUV;
  vColor = uInstanced ? aColor * aInstanceTint : aColor;
}
