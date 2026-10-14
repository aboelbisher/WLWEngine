// Tests for procedural meshes in wlw/utils/primitives.h (CPU-side only: nothing is uploaded).
#include <glm/glm.hpp>

#include "test_helpers.h"
#include "wlw/utils/primitives.h"

TEST_SUITE_BEGIN("primitives");

using namespace wlw;

namespace {

glm::vec3 ToGlm(const core::Vector3& v) { return {v.x, v.y, v.z}; }

// Checks a triangle soup (3 vertices per triangle) is a proper closed solid:
// - each triangle's winding agrees with its stored normal (front faces point outwards),
// - normals are unit length,
// - area-weighted normals sum to zero (true only for a closed surface),
// and returns its volume (divergence theorem: sum of signed tetrahedra from the origin).
float CheckClosedSolid(const core::Mesh<core::Vertex3D>& mesh) {
  const auto& v = mesh.GetVertices();
  CHECK(v.size() % 3 == 0);
  glm::vec3 normal_sum(0.0f);
  float volume = 0.0f;
  for (size_t i = 0; i + 2 < v.size(); i += 3) {
    const glm::vec3 a = ToGlm(v[i].position), b = ToGlm(v[i + 1].position), c = ToGlm(v[i + 2].position);
    const glm::vec3 area_normal = glm::cross(b - a, c - a) / 2.0f;
    const glm::vec3 stored = ToGlm(v[i].normal);
    CHECK(Near(glm::length(stored), 1.0f));
    CHECK(glm::dot(area_normal, stored) > 0.0f);  // counter-clockwise seen from outside
    normal_sum += area_normal;
    volume += glm::dot(a, glm::cross(b, c)) / 6.0f;
  }
  CHECK(glm::length(normal_sum) < 1e-4f);
  return volume;
}

}  // namespace

TEST_CASE("beveled cube") {
  const float size = 2.0f, bevel = 0.25f;
  const auto mesh = utils::createBeveledCubeWithNormals(size, bevel, {1, 0, 0, 1});
  // 6 faces x 2 triangles + 12 edge strips x 2 + 8 corners x 1 = 44 triangles.
  CHECK(mesh->GetVertices().size() == 44 * 3);
  CHECK(mesh->GetIndices().size() == mesh->GetVertices().size());

  const auto& box = mesh->GetLocalAABB();  // outer size unchanged by the bevel
  CHECK((Near(box.min.x, -1) && Near(box.max.x, 1) && Near(box.min.y, -1) && Near(box.max.z, 1)));

  // A chamfered cube = full cube - 12 edge prisms (triangle b*b/2, length s-2b) - what's cut from
  // the 8 corner b*b*b cubes (only the tetrahedron b^3/6 under the corner triangle remains, so
  // 5/6 b^3 is removed from each).
  const float expected = size * size * size - 12.0f * (bevel * bevel / 2.0f) * (size - 2.0f * bevel) -
                         8.0f * (5.0f * bevel * bevel * bevel / 6.0f);
  CHECK(Near(CheckClosedSolid(*mesh), expected, 1e-3f));
}

TEST_CASE("plain cube still closed") {
  // The original cube, as a reference for what CheckClosedSolid accepts.
  const auto mesh = utils::createCubeWithNormals(2.0f);
  CHECK(Near(CheckClosedSolid(*mesh), 8.0f, 1e-3f));
}

TEST_CASE("plane") {
  // 4 corners, 2 triangles, both facing up (counter-clockwise seen from above), spanning the size.
  const auto mesh = utils::createPlaneWithNormals(4.0f, 2.0f);
  const auto& v = mesh->GetVertices();
  const auto& index = mesh->GetIndices();
  CHECK((v.size() == 4 && index.size() == 6));
  for (size_t i = 0; i + 2 < index.size(); i += 3) {
    const glm::vec3 a = ToGlm(v[index[i]].position), b = ToGlm(v[index[i + 1]].position), c = ToGlm(v[index[i + 2]].position);
    CHECK(glm::cross(b - a, c - a).y > 0.0f);
  }
  for (const auto& vertex : v) CHECK((Near(vertex.normal.y, 1.0f) && Near(std::abs(vertex.position.x), 2.0f) && Near(std::abs(vertex.position.z), 1.0f)));
}

TEST_SUITE_END();
