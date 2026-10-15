// Tests for core::NormalMatrix (core/normal_matrix.h): the cofactor normal matrix gives the same
// normals as the textbook transpose(inverse(m)), points them out of mirrored surfaces and stays
// finite when a transform is squashed flat.
#include <cmath>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include "test_helpers.h"
#include "wlw/core/normal_matrix.h"
#include "wlw/core/random.h"

TEST_SUITE_BEGIN("normal_matrix");

using namespace wlw;

namespace {

bool NearVec(const glm::vec3& a, const glm::vec3& b, float tolerance = 1e-4f) {
  return glm::length(a - b) <= tolerance;
}

bool Finite(const glm::mat3& m) {
  for (int c = 0; c < 3; ++c) {
    for (int r = 0; r < 3; ++r) {
      if (!std::isfinite(m[c][r])) return false;
    }
  }
  return true;
}

// A random transform's 3x3 part: rotation x shear x scale (scales 0.2-3, each mirrored at random).
glm::mat3 RandomTransform(core::Random& random) {
  const glm::vec3 axis = glm::normalize(glm::vec3(random.Range(-1, 1), random.Range(-1, 1), random.Range(-1, 1)) +
                                        glm::vec3(0.0f, 0.0f, 1e-3f));
  const glm::mat3 rotation(glm::rotate(glm::mat4(1.0f), random.Range(0.0f, 6.28f), axis));
  glm::mat3 shear(1.0f);
  shear[1][0] = random.Range(-0.8f, 0.8f);
  shear[2][0] = random.Range(-0.8f, 0.8f);
  shear[2][1] = random.Range(-0.8f, 0.8f);
  glm::vec3 scale(random.Range(0.2f, 3.0f), random.Range(0.2f, 3.0f), random.Range(0.2f, 3.0f));
  for (int i = 0; i < 3; ++i) {
    if (random.Chance(0.25f)) scale[i] = -scale[i];
  }
  return rotation * shear * glm::mat3(glm::scale(glm::mat4(1.0f), scale));
}

// The textbook normal of `normal` under `m`, in double precision, unit length.
glm::vec3 ReferenceNormal(const glm::mat3& m, const glm::vec3& normal) {
  const glm::dmat3 inverse_transpose = glm::transpose(glm::inverse(glm::dmat3(m)));
  return glm::vec3(glm::normalize(inverse_transpose * glm::dvec3(normal)));
}

}  // namespace

TEST_CASE("normal matrix matches inverse-transpose") {
  core::Random random(1234);
  int mirrored = 0;
  for (int i = 0; i < 1000; ++i) {
    const glm::mat3 m = RandomTransform(random);
    if (glm::determinant(m) < 0.0f) ++mirrored;
    const glm::mat3 normal_matrix = core::NormalMatrix(m);
    for (int j = 0; j < 4; ++j) {
      const glm::vec3 normal = glm::normalize(glm::vec3(random.Range(-1, 1), random.Range(-1, 1), 1.0f));
      const glm::vec3 expected = ReferenceNormal(m, normal);
      const glm::vec3 actual = glm::normalize(normal_matrix * normal);
      if (!NearVec(actual, expected, 1e-4f)) {
        FAIL_CHECK("transform " << i << ": normals differ by " << glm::length(actual - expected));
        break;
      }
    }
  }
  CHECK(mirrored > 100);  // both signs were tried
}

TEST_CASE("normal matrix of a rotation is the rotation") {
  const glm::mat4 rotation = glm::rotate(glm::mat4(1.0f), 0.7f, glm::normalize(glm::vec3(1, 2, 3)));
  const glm::mat3 normal_matrix = core::NormalMatrix(glm::translate(glm::mat4(1.0f), {5, -2, 9}) * rotation);
  for (int c = 0; c < 3; ++c) CHECK(NearVec(normal_matrix[c], glm::mat3(rotation)[c], 1e-6f));
}

TEST_CASE("normal matrix points mirrored normals outward") {
  // Mirrored in x: a cube's +x face ends up at -x, and its normal must point to -x (out of the cube).
  const glm::mat3 mirror(glm::scale(glm::mat4(1.0f), {-1.0f, 1.0f, 1.0f}));
  CHECK(NearVec(core::NormalMatrix(mirror) * glm::vec3(1, 0, 0), {-1, 0, 0}));
  CHECK(NearVec(core::NormalMatrix(mirror) * glm::vec3(0, 1, 0), {0, 1, 0}));
  // The bare cofactor matrix (no sign) would point it into the cube.
  const glm::mat3 bare(glm::cross(mirror[1], mirror[2]), glm::cross(mirror[2], mirror[0]),
                       glm::cross(mirror[0], mirror[1]));
  CHECK(NearVec(bare * glm::vec3(1, 0, 0), {1, 0, 0}));
  // Mirrored twice = a rotation: nothing flipped.
  const glm::mat3 twice(glm::scale(glm::mat4(1.0f), {-1.0f, -1.0f, 1.0f}));
  CHECK(NearVec(core::NormalMatrix(twice) * glm::vec3(1, 0, 0), {-1, 0, 0}));
  CHECK(NearVec(core::NormalMatrix(twice) * glm::vec3(0, 0, 1), {0, 0, 1}));
}

TEST_CASE("normal matrix of a flat transform is finite") {
  // Squashed flat on y (scale 0): the inverse doesn't exist, the cofactor does. A side normal turns
  // to 0; a top normal stays up (the flat shape's only face direction).
  const glm::mat3 flat(glm::scale(glm::mat4(1.0f), {2.0f, 0.0f, 3.0f}));
  const glm::mat3 normal_matrix = core::NormalMatrix(flat);
  CHECK(Finite(normal_matrix));
  CHECK(NearVec(glm::normalize(normal_matrix * glm::vec3(0, 1, 0)), {0, 1, 0}));
  CHECK(NearVec(normal_matrix * glm::vec3(1, 0, 0), {0, 0, 0}));
  // Squashed on two axes: everything is 0, still finite.
  CHECK(Finite(core::NormalMatrix(glm::mat3(glm::scale(glm::mat4(1.0f), {0.0f, 0.0f, 3.0f})))));
}

TEST_CASE("normal matrix of a product is the product") {
  // What instanced draws rely on: the node's part on the CPU times each copy's part per vertex.
  core::Random random(99);
  for (int i = 0; i < 100; ++i) {
    const glm::mat3 a = RandomTransform(random);
    const glm::mat3 b = RandomTransform(random);
    const glm::mat3 product = core::NormalMatrix(a * b);
    const glm::mat3 parts = core::NormalMatrix(a) * core::NormalMatrix(b);
    for (int c = 0; c < 3; ++c) {
      const float size = glm::length(product[c]) + 1.0f;
      CHECK(NearVec(product[c], parts[c], 1e-4f * size));
    }
  }
}
