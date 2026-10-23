// Tests for screen -> world picking: Camera3D::ScreenPointToRay and Raycast against a plane.
// Method: project known world points to pixels with the camera's own matrices (what the GPU
// does), then pick them back and check we land on the same point.
#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <memory>
#include <optional>

#include "test_helpers.h"
#include "wlw/core/deprecated.h"
#include "wlw/core/geometry.h"
#include "wlw/scene/camera_3d.h"
#include "wlw/scene/orthographic_camera.h"

TEST_SUITE_BEGIN("picking");

using namespace wlw;

namespace {

const Vec2 kScreen{540.0f, 960.0f};

[[maybe_unused]] bool NearMat(const glm::mat4& a, const glm::mat4& b, float tolerance) {
  for (int c = 0; c < 4; ++c) {
    for (int r = 0; r < 4; ++r) {
      if (!Near(a[c][r], b[c][r], tolerance)) return false;
    }
  }
  return true;
}

std::shared_ptr<scene::Camera3D> Perspective() {
  auto camera = scene::Camera3D::Create();
  camera->SetPerspective(glm::radians(60.0f), 0.1f, 100.0f);
  camera->SetPosition({0, 6, 8});
  camera->LookAt({0, 0, 0});
  return camera;
}

std::shared_ptr<scene::Camera3D> OrthographicTilted() {
  auto camera = scene::Camera3D::Create();
  camera->SetOrthographic(13.0f, 0.1f, 100.0f);
  camera->SetFrameAspect(9.0f / 13.0f);
  camera->SetFitMode(scene::Camera3D::FitMode::Contain);
  camera->SetPosition({0, 12, 5});
  camera->LookAt({0, 0, 0});
  return camera;
}

Vec2 WorldToScreen(const scene::Camera3D& camera, const glm::vec3& world) {
  const glm::vec4 clip = camera.GetViewProjectionMatrix(kScreen.x / kScreen.y) * glm::vec4(world, 1.0f);
  const glm::vec3 ndc = glm::vec3(clip) / clip.w;
  return {(ndc.x + 1.0f) / 2.0f * kScreen.x, (1.0f - ndc.y) / 2.0f * kScreen.y};  // y flipped
}

// The point where the ray meets the plane.
std::optional<glm::vec3> HitPlane(const Ray& ray, const Plane& plane) {
  if (const std::optional<float> t = Raycast(ray, plane)) return ray.At(*t);
  return std::nullopt;
}

void CheckRoundTrip(const scene::Camera3D& camera, const glm::vec3& world) {
  const Ray ray = camera.ScreenPointToRay(WorldToScreen(camera, world), kScreen);
  CHECK(Near(glm::length(ray.direction), 1.0f));
  const auto hit = HitPlane(ray, {{0.0f, 1.0f, 0.0f}, 0.0f});
  CHECK(hit.has_value());
  if (hit) CHECK(glm::length(*hit - world) < 1e-3f);
}

const glm::vec3 kGroundPoints[] = {{0, 0, 0}, {-3.5f, 0, -3.5f}, {3.5f, 0, 3.5f}, {2.0f, 0, 5.5f}};

}  // namespace

TEST_CASE("orthographic tilted") {
  auto camera = OrthographicTilted();
  for (const glm::vec3& p : kGroundPoints) CheckRoundTrip(*camera, p);

  // Orthographic: every ray points the same way.
  const glm::vec3 a = camera->ScreenPointToRay({0, 0}, kScreen).direction;
  const glm::vec3 b = camera->ScreenPointToRay({500, 900}, kScreen).direction;
  CHECK(glm::length(a - b) < 1e-4f);
}

TEST_CASE("perspective") {
  auto camera = Perspective();
  for (const glm::vec3& p : kGroundPoints) CheckRoundTrip(*camera, p);

  // Perspective: rays fan out from the eye.
  const glm::vec3 a = camera->ScreenPointToRay({0, 0}, kScreen).direction;
  const glm::vec3 b = camera->ScreenPointToRay({500, 900}, kScreen).direction;
  CHECK(glm::length(a - b) > 0.1f);
}

TEST_CASE("screen center and flip") {
  auto camera = scene::Camera3D::Create();
  camera->SetOrthographic(10.0f, 0.1f, 100.0f);
  camera->SetFitMode(scene::Camera3D::FitMode::Contain);
  camera->SetPosition({0, 10, 0});
  camera->LookAt({0, 0, 0}, {0, 0, -1});  // straight down: screen up = -z
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
  auto ortho = OrthographicTilted();
  auto persp = Perspective();
  for (const scene::Camera3D* camera : {ortho.get(), persp.get()}) {
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

#if !WLW_NO_DEPRECATED
WLW_DISABLE_DEPRECATION_WARNINGS_BEGIN
TEST_CASE("deprecated orthographic camera matches the 0.3 matrices") {
  auto camera = scene::OrthographicCamera::Create();
  camera->SetPosition({0, 12, 5});
  camera->SetTarget({1, 0, 0});
  camera->SetVisibleArea(9, 13);
  camera->SetClipPlanes(0.5f, 50.0f);
  // 0.3: glm::lookAt, then the visible area fitted to the aspect (the height grown until the width fits).
  const float aspect = kScreen.x / kScreen.y;
  const float half_height = std::max(13.0f / 2.0f, (9.0f / 2.0f) / aspect);
  const float half_width = half_height * aspect;
  const glm::mat4 view = glm::lookAt(glm::vec3(0, 12, 5), glm::vec3(1, 0, 0), glm::vec3(0, 1, 0));
  const glm::mat4 projection = glm::ortho(-half_width, half_width, -half_height, half_height, 0.5f, 50.0f);
  CHECK(NearMat(camera->GetViewMatrix(), view, 1e-4f));
  CHECK(NearMat(camera->GetProjectionMatrix(aspect), projection, 1e-4f));
  // A wide screen: the height is what fits.
  CHECK(Near(camera->VisibleSize(2.0f).y, 13.0f, 1e-4f));
  camera->SetUp({0, 0, -1});
  camera->SetPosition({0, 10, 0});
  camera->SetTarget({0, 0, 0});
  CHECK(NearMat(camera->GetViewMatrix(), glm::lookAt(glm::vec3(0, 10, 0), glm::vec3(0), glm::vec3(0, 0, -1)), 1e-4f));
}
WLW_DISABLE_DEPRECATION_WARNINGS_END
#endif

TEST_SUITE_END();
