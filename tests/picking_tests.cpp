// Tests for screen -> world picking: Camera3D::ScreenPointToRay and Raycast against a plane.
// Method: project known world points to pixels with the camera's own matrices (what the GPU
// does), then pick them back and check we land on the same point.
#include <glm/gtc/matrix_transform.hpp>

#include <optional>

#include "test_helpers.h"
#include "wlw/core/geometry.h"
#include "wlw/scene/orthographic_camera.h"

TEST_SUITE_BEGIN("picking");

using namespace wlw;

namespace {

const Vec2 kScreen{540.0f, 960.0f};

// The smallest possible perspective camera, to check the same code handles rays that fan out.
class PerspectiveTestCamera : public scene::Camera3D {
 public:
  void SetPosition(const Vec3&) override {}
  void SetTarget(const Vec3&) override {}
  void UpdateSize(const Vec2&) override {}
  const Vec3& GetPosition() const override { return position_; }
  const glm::mat4& GetViewMatrix() override { return view_; }
  const glm::mat4& GetProjectionMatrix() override { return projection_; }
  void ProcessKeyboard(scene::CameraMovement, float) override {}
  void ProcessMouseMovement(float, float, bool) override {}

 private:
  Vec3 position_{0.0f, 6.0f, 8.0f};
  glm::mat4 view_ = glm::lookAt(glm::vec3(0, 6, 8), glm::vec3(0), glm::vec3(0, 1, 0));
  glm::mat4 projection_ = glm::perspective(glm::radians(60.0f), kScreen.x / kScreen.y, 0.1f, 100.0f);
};

Vec2 WorldToScreen(scene::Camera3D& camera, const glm::vec3& world) {
  const glm::vec4 clip = camera.GetProjectionMatrix() * camera.GetViewMatrix() * glm::vec4(world, 1.0f);
  const glm::vec3 ndc = glm::vec3(clip) / clip.w;
  return {(ndc.x + 1.0f) / 2.0f * kScreen.x, (1.0f - ndc.y) / 2.0f * kScreen.y};  // y flipped
}

// The point where the ray meets the plane.
std::optional<glm::vec3> HitPlane(const Ray& ray, const Plane& plane) {
  if (const std::optional<float> t = Raycast(ray, plane)) return ray.At(*t);
  return std::nullopt;
}

void CheckRoundTrip(scene::Camera3D& camera, const glm::vec3& world) {
  const Ray ray = camera.ScreenPointToRay(WorldToScreen(camera, world), kScreen);
  CHECK(Near(glm::length(ray.direction), 1.0f));
  const auto hit = HitPlane(ray, {{0.0f, 1.0f, 0.0f}, 0.0f});
  CHECK(hit.has_value());
  if (hit) CHECK(glm::length(*hit - world) < 1e-3f);
}

const glm::vec3 kGroundPoints[] = {{0, 0, 0}, {-3.5f, 0, -3.5f}, {3.5f, 0, 3.5f}, {2.0f, 0, 5.5f}};

}  // namespace

TEST_CASE("orthographic tilted") {
  auto camera = scene::OrthographicCamera::Create();
  camera->SetPosition({0, 12, 5});
  camera->SetTarget({0, 0, 0});
  camera->SetVisibleArea(9, 13);
  camera->UpdateSize(kScreen);
  for (const glm::vec3& p : kGroundPoints) CheckRoundTrip(*camera, p);

  // Orthographic: every ray points the same way.
  const glm::vec3 a = camera->ScreenPointToRay({0, 0}, kScreen).direction;
  const glm::vec3 b = camera->ScreenPointToRay({500, 900}, kScreen).direction;
  CHECK(glm::length(a - b) < 1e-4f);
}

TEST_CASE("perspective") {
  PerspectiveTestCamera camera;
  for (const glm::vec3& p : kGroundPoints) CheckRoundTrip(camera, p);

  // Perspective: rays fan out from the eye.
  const glm::vec3 a = camera.ScreenPointToRay({0, 0}, kScreen).direction;
  const glm::vec3 b = camera.ScreenPointToRay({500, 900}, kScreen).direction;
  CHECK(glm::length(a - b) > 0.1f);
}

TEST_CASE("screen center and flip") {
  auto camera = scene::OrthographicCamera::Create();
  camera->SetPosition({0, 10, 0});
  camera->SetTarget({0, 0, 0});
  camera->SetUp({0, 0, -1});  // straight down: screen up = -z
  camera->SetVisibleArea(10, 10);
  camera->UpdateSize(kScreen);
  const auto center = HitPlane(camera->ScreenPointToRay({270, 480}, kScreen), {{0, 1, 0}, 0});
  CHECK((center && glm::length(*center) < 1e-4f));
  // Top of the screen (small pixel y) must be screen-up (-z), not -down: catches a missing flip.
  const auto top = HitPlane(camera->ScreenPointToRay({270, 100}, kScreen), {{0, 1, 0}, 0});
  CHECK((top && top->z < -1.0f));
}

TEST_CASE("intersect plane") {
  const Plane ground{{0, 1, 0}, 0};
  CHECK(!HitPlane({{0, 1, 0}, {1, 0, 0}}, ground));  // parallel
  CHECK(!HitPlane({{0, 1, 0}, {0, 1, 0}}, ground));  // pointing away
  const auto hit = HitPlane({{1, 5, 2}, {0, -1, 0}}, ground);
  CHECK((hit && Near(hit->x, 1) && Near(hit->y, 0) && Near(hit->z, 2)));

  const Plane raised = Plane::FromPointAndNormal({0, 2, 0}, {0, 1, 0});  // y = 2
  const auto raised_hit = HitPlane({{0, 5, 0}, {0, -1, 0}}, raised);
  CHECK((raised_hit && Near(raised_hit->y, 2)));
}

TEST_CASE("world to screen is the inverse") {
  auto ortho = scene::OrthographicCamera::Create();
  ortho->SetPosition({0, 12, 5});
  ortho->SetTarget({0, 0, 0});
  ortho->SetVisibleArea(9, 13);
  ortho->UpdateSize(kScreen);
  PerspectiveTestCamera persp;
  for (scene::Camera3D* camera : {static_cast<scene::Camera3D*>(ortho.get()), static_cast<scene::Camera3D*>(&persp)}) {
    for (const glm::vec3& world : kGroundPoints) {
      // WorldToScreen matches the GPU's own projection...
      const glm::vec3 screen = camera->WorldToScreen(world, kScreen);
      const Vec2 expected = WorldToScreen(*camera, world);
      CHECK((Near(screen.x, expected.x, 1e-2f) && Near(screen.y, expected.y, 1e-2f)));
      CHECK((screen.z > -1.0f && screen.z < 1.0f));  // between near and far: visible
      // ...and picking that pixel lands back on the point.
      const auto hit = HitPlane(camera->ScreenPointToRay({screen.x, screen.y}, kScreen), {{0, 1, 0}, 0});
      CHECK((hit && glm::length(*hit - world) < 1e-3f));
    }
  }
}

TEST_SUITE_END();
