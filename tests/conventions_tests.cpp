// Tests for the engine's conventions (Roadmap 2.4, core/math.h): Z up, +Y forward, +X right, right-handed, radians,
// counter-clockwise front faces. Each system that has a direction in it is checked against the same axes: the
// constants, rotations, nodes, the default camera on screen, lights, particles, the primitives and glTF import.
#include <cmath>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include <glm/glm.hpp>

#include "test_helpers.h"
#include "wlw/core/math.h"
#include "wlw/core/random.h"
#include "wlw/core/rotation.h"
#include "wlw/fx/particle_system.h"
#include "wlw/scene/camera_3d.h"
#include "wlw/scene/light.h"
#include "wlw/scene/node.h"
#include "wlw/utils/gltf_loader.h"
#include "wlw/utils/primitives.h"

TEST_SUITE_BEGIN("conventions");

using namespace wlw;

namespace {

bool NearVec(const Vec3& a, const Vec3& b, float tolerance = 1e-4f) { return glm::length(a - b) <= tolerance; }

using MeshPtr = std::shared_ptr<core::Mesh<core::Vertex3D>>;

// Every triangle (indexed, or 3 vertices each) winds counter-clockwise seen from outside: its winding's normal points
// the way the vertex normals do, and away from the centre (all these meshes are convex and centred on the origin).
// Returns how many triangles don't.
int InwardTriangles(const MeshPtr& mesh) {
  const auto& v = mesh->GetVertices();
  std::vector<uint32_t> index = mesh->GetIndices();
  if (index.empty()) {
    for (uint32_t i = 0; i < v.size(); ++i) index.push_back(i);
  }
  REQUIRE(index.size() % 3 == 0);
  int wrong = 0;
  for (size_t i = 0; i < index.size(); i += 3) {
    const core::Vertex3D& a = v[index[i]];
    const core::Vertex3D& b = v[index[i + 1]];
    const core::Vertex3D& c = v[index[i + 2]];
    const Vec3 winding = glm::cross(b.position - a.position, c.position - a.position);
    if (glm::length(winding) < 1e-9f) continue;  // a sliver at a pole
    const Vec3 centre = (a.position + b.position + c.position) / 3.0f;
    if (glm::dot(winding, a.normal + b.normal + c.normal) <= 0.0f || glm::dot(winding, centre) <= 0.0f) ++wrong;
  }
  return wrong;
}

}  // namespace

TEST_CASE("axes are right-handed") {
  CHECK(NearVec(glm::cross(kRight, kForward), kUp));  // Right x Forward = Up
  CHECK(NearVec(glm::cross(kForward, kUp), kRight));
  CHECK(NearVec(glm::cross(kUp, kRight), kForward));
  CHECK((kLeft == -kRight && kBack == -kForward && kDown == -kUp && kModelFront == kBack));
  CHECK(glm::determinant(Mat3(kRight, kForward, kUp)) == 1.0f);
}

TEST_CASE("from a Y-up file and back") {
  // glTF / OBJ: +Y up, -Z forward, +Z = a model's front. A quarter turn, so lengths and handedness stay.
  CHECK(FromYUp({0, 1, 0}) == kUp);
  CHECK(FromYUp({0, 0, -1}) == kForward);
  CHECK(FromYUp({0, 0, 1}) == kModelFront);
  CHECK(FromYUp({1, 0, 0}) == kRight);
  CHECK(glm::determinant(Mat3(FromYUp({1, 0, 0}), FromYUp({0, 1, 0}), FromYUp({0, 0, 1}))) == 1.0f);
  const Vec3 p{0.3f, -2.0f, 5.0f};
  CHECK(ToYUp(FromYUp(p)) == p);
  CHECK(FromYUp(ToYUp(p)) == p);
}

TEST_CASE("positive turns are counter-clockwise looking down the axis") {
  // Yaw: from above (looking down -Z at the ground), +Y turns towards -X.
  CHECK(NearVec(glm::angleAxis(kHalfPi, kUp) * kForward, kLeft));
  CHECK(NearVec(glm::angleAxis(kHalfPi, kRight) * kForward, kUp));    // a positive pitch looks up
  CHECK(NearVec(glm::angleAxis(kHalfPi, kForward) * kRight, kDown));  // a positive roll tips the right side down
  // The default Euler order is yaw (z), pitch (x), roll (y): the yaw turns the pitched direction.
  const Quat q = QuatFromEuler({0.5f, 0.0f, kHalfPi});
  const Vec3 forward = q * kForward;
  CHECK((forward.z > 0.4f && forward.x < -0.8f && std::abs(forward.y) < 1e-5f));  // up a little, towards -X
}

TEST_CASE("nodes: forward +Y, right +X, up +Z, for any rotation") {
  scene::Node3D node;
  CHECK((NearVec(node.Forward(), kForward) && NearVec(node.Right(), kRight) && NearVec(node.Up(), kUp)));
  node.SetEulerAngles({0.0f, 0.0f, kHalfPi});  // a quarter yaw
  CHECK(NearVec(node.Forward(), kLeft));
  core::Random rng(11);
  for (int i = 0; i < 50; ++i) {
    node.SetEulerAngles({rng.Range(-3.0f, 3.0f), rng.Range(-3.0f, 3.0f), rng.Range(-3.0f, 3.0f)});
    CHECK(NearVec(glm::cross(node.Right(), node.Forward()), node.Up()));
  }
  node.SetPosition({0, 0, 0});
  node.LookAt({0, 10, 0});  // looking along +Y: no turn at all
  CHECK(NearVec(node.Up(), kUp));
  node.LookAt({-3, 0, 0});
  CHECK((NearVec(node.Forward(), kLeft) && NearVec(node.Up(), kUp)));  // up stays world up
}

TEST_CASE("default camera: +X right, +Z up on screen, +Y ahead") {
  scene::Camera3D camera;
  const Vec2 size{200.0f, 100.0f};
  const Vec3 ahead = camera.WorldToScreen({0, 5, 0}, size);
  CHECK((Near(ahead.x, 100.0f, 1e-3f) && Near(ahead.y, 50.0f, 1e-3f)));  // the middle
  CHECK((ahead.z > -1.0f && ahead.z < 1.0f));                            // between near and far
  CHECK(camera.WorldToScreen({1, 5, 0}, size).x > 100.0f);               // +X: right
  CHECK(camera.WorldToScreen({0, 5, 1}, size).y < 50.0f);                // +Z: up (screen y grows down)
  CHECK(camera.WorldToScreen({0, -5, 0}, size).z > 1.0f);                // behind: clipped
  const Ray ray = camera.ScreenPointToRay({100.0f, 50.0f}, size);
  CHECK(NearVec(ray.direction, kForward));
  CHECK(Overlaps(camera.GetFrustum(2.0f), AABB::FromCenterSize({0, 5, 0}, Vec3(1))));
  CHECK_FALSE(Overlaps(camera.GetFrustum(2.0f), AABB::FromCenterSize({0, -5, 0}, Vec3(1))));
}

TEST_CASE("lights shine along forward, straight down by default") {
  scene::DirectionalLight3D sun;
  scene::SpotLight3D spot;
  CHECK((NearVec(sun.GetDirection(), kDown) && NearVec(spot.GetDirection(), kDown)));
  sun.SetDirection({0.0f, 1.0f, -1.0f});
  CHECK(NearVec(sun.GetDirection(), glm::normalize(Vec3(0.0f, 1.0f, -1.0f))));
  CHECK(NearVec(sun.GetDirection(), sun.Forward()));
}

TEST_CASE("particles fall down -Z and rest on a floor at a height in z") {
  fx::ParticleSystem system(1);
  system.drag = 0.0f;
  system.floor = fx::ParticleSystem::Floor{.height = 2.0f};
  fx::ParticleSystem::Particle p;
  p.position = {0.0f, 0.0f, 3.0f};
  p.size_start = p.size_end = 0.2f;
  p.lifetime = 100.0f;
  system.Emit(p);
  for (int i = 0; i < 600; ++i) system.Update(1.0f / 60.0f);
  const fx::ParticleSystem::Particle& rested = system.Particles()[0];
  CHECK(Near(rested.position.z, 2.1f, 1e-3f));  // its bottom on the floor
  CHECK((rested.position.x == 0.0f && rested.position.y == 0.0f));
}

TEST_CASE("primitives: counter-clockwise outward faces, standing on Z") {
  const std::pair<std::string, std::function<MeshPtr()>> meshes[] = {
      {"cube", [] { return utils::createCubeWithNormals(1.0f); }},
      {"beveled cube", [] { return utils::createBeveledCubeWithNormals(1.0f, 0.1f); }},
      {"pyramid frustum", [] { return utils::createPyramidFrustumWithNormals(2.0f, 1.0f, 1.5f); }},
      {"sphere", [] { return utils::createSphereWithNormals(1.0f, 8, 16); }},
      {"cylinder", [] { return utils::createCylinderWithNormals(0.5f, 2.0f, 12); }},
      {"gem", [] { return utils::createGemWithNormals(0.5f, 1.0f); }},
  };
  for (const auto& [name, make] : meshes) {
    CAPTURE(name);
    CHECK(InwardTriangles(make()) == 0);
  }
  // The tall ones are tall along Z.
  const AABB cylinder = utils::createCylinderWithNormals(0.5f, 2.0f, 12)->GetLocalAABB();
  CHECK((Near(cylinder.max.z, 1.0f) && Near(cylinder.max.x, 0.5f, 1e-3f) && Near(cylinder.max.y, 0.5f, 1e-2f)));
  const AABB gem = utils::createGemWithNormals(0.5f, 1.0f)->GetLocalAABB();
  CHECK((Near(gem.max.z, 0.5f) && Near(gem.min.z, -0.5f) && Near(gem.max.x, 0.25f)));
  const AABB pyramid = utils::createPyramidFrustumWithNormals(2.0f, 1.0f, 1.5f)->GetLocalAABB();
  CHECK(Near(pyramid.max.z, 0.75f));
  // (Each mesh in a local: a range-for over a temporary's member would dangle before C++23.)
  const MeshPtr sphere = utils::createSphereWithNormals(1.0f, 8, 16);
  bool pole = false;
  for (const auto& v : sphere->GetVertices()) pole |= NearVec(v.position, kUp);
  CHECK(pole);
  // The plane is the ground: XY, facing +Z.
  const MeshPtr plane = utils::createPlaneWithNormals(2.0f, 2.0f);
  for (const auto& v : plane->GetVertices()) CHECK(v.normal == kUp);
  // The cube's front (as written, +Z) faces kModelFront.
  bool front = false;
  const MeshPtr cube = utils::createCubeWithNormals(1.0f);
  for (const auto& v : cube->GetVertices()) front |= NearVec(v.normal, kModelFront);
  CHECK(front);
}

TEST_CASE("glTF models come in Z up, facing -Y") {
  // One triangle facing glTF's front (+Z), from (0,0,0) along +X and +Y (glTF's up).
  const auto model = utils::GLTFLoader::LoadModel(WLW_TEST_DATA_DIR "/front_triangle.gltf", false);
  REQUIRE(model);
  REQUIRE(model->meshes.size() == 1);
  const auto& v = model->meshes[0]->GetVertices();
  REQUIRE(v.size() == 3);
  CHECK(NearVec(v[1].position, kRight));
  CHECK(NearVec(v[2].position, kUp));  // glTF's +Y is up here too
  for (const auto& vertex : v) CHECK(NearVec(vertex.normal, kModelFront));
  // Still counter-clockwise seen from its front.
  CHECK(glm::dot(glm::cross(v[1].position - v[0].position, v[2].position - v[0].position), kModelFront) > 0.0f);
}

TEST_SUITE_END();
