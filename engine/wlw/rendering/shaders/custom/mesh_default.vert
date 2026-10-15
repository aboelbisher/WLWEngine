// The vertex stage of a mesh shader without its own vertex(): the usual transform.
void main() {
  highp vec4 world_pos = WorldPosition(aPos);
  gl_Position = uViewProjection * world_pos;
  vWorldPos = world_pos.xyz;
  vNormal = WorldNormal(aNormal);
  vUV = aUV;
  vColor = uInstanced ? aColor * aInstanceTint : aColor;
}
