#pragma once

#include <glm/glm.hpp>

namespace wlw::core {

// The matrix that carries normals through a transform: NormalMatrix(model) * normal is the normal in
// world space, pointing the right way but not unit length (normalize it).
//
// Normals can't be moved like positions: squash a sphere into an egg and its normals must tilt
// towards the squashed axis, not away from it. The textbook matrix is transpose(inverse(m)). This is
// its cofactor matrix instead (three cross products of m's columns), which equals
// det(m) * transpose(inverse(m)), negated when det(m) < 0, so both give the same normals once
// normalized. But nothing is divided by the determinant:
//   - a transform squashed flat (a scale of 0 on an axis) still gives finite normals, along the flat
//     axis, where the inverse is inf / NaN;
//   - it's about 4x cheaper than glm::inverse of the mat4 (3 cross products).
// The sign is for mirrored transforms (a negative scale, det < 0): the cofactor alone follows the
// mirrored triangles' winding and points normals into the surface. Lighting wants them out of it, so
// they're flipped back (like Filament's renderer).
// Products keep it: NormalMatrix(a * b) == NormalMatrix(a) * NormalMatrix(b) (a and b invertible), so
// an instanced draw takes the node's part from the CPU and each copy's part per vertex.
// GLSL twin: NormalMatrix() in rendering/shaders/wlw/common.glsl.
inline glm::mat3 NormalMatrix(const glm::mat3& m) {
  glm::vec3 x = glm::cross(m[1], m[2]);
  glm::vec3 y = glm::cross(m[2], m[0]);
  glm::vec3 z = glm::cross(m[0], m[1]);
  if (glm::dot(m[0], x) < 0.0f) {  // det(m) < 0: mirrored
    x = -x;
    y = -y;
    z = -z;
  }
  return glm::mat3(x, y, z);
}

// The same for a 4x4 transform: its 3x3 part (translation doesn't turn normals).
inline glm::mat3 NormalMatrix(const glm::mat4& transform) { return NormalMatrix(glm::mat3(transform)); }

}  // namespace wlw::core
