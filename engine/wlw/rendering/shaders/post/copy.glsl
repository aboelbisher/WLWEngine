// A post pass that copies its input (what a pass without a usable shader does).
shader_type post;

void fragment() {
  FragColor = texture(uSource, vUV);
}
