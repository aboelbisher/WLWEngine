// Tests for Camera3D as a node (Roadmap 2.3): the view matrix against the node's world transform, the projections
// and fit modes at portrait, square and landscape aspects, the frustum, and the camera controllers (fly, orbit,
// follow) driven by input structs, without a window.
#include <cmath>
#include <memory>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include "test_helpers.h"
#include "wlw/core/geometry.h"
#include "wlw/core/math.h"
#include "wlw/scene/camera_3d.h"
#include "wlw/scene/camera_controllers.h"
#include "wlw/scene/node.h"

TEST_SUITE_BEGIN("camera");

using namespace wlw;
using scene::Camera3D;
using FitMode = scene::Camera3D::FitMode;

namespace {

bool NearVec(const Vec3& a, const Vec3& b, float tolerance = 1e-4f) { return glm::length(a - b) <= tolerance; }

bool NearMat(const Mat4& a, const Mat4& b, float tolerance = 1e-4f) {
  for (int c = 0; c < 4; ++c) {
    for (int r = 0; r < 4; ++r) {
      if (!Near(a[c][r], b[c][r], tolerance)) return false;
    }
  }
  return true;
}

// Where a camera-space point lands in NDC.
Vec3 Ndc(const Mat4& projection, const Vec3& camera_space) {
  const Vec4 clip = projection * Vec4(camera_space, 1.0f);
  return Vec3(clip) / clip.w;
}

constexpr float kAspects[] = {0.5f, 1.0f, 2.0f};  // portrait, square, landscape

}  // namespace

TEST_CASE("default camera looks down -z") {
  Camera3D camera;
  CHECK(camera.Kind() == scene::NodeKind::Camera);
  CHECK((camera.GetProjection() == Camera3D::Projection::Perspective && camera.GetFitMode() == FitMode::KeepHeight));
  CHECK((Near(camera.GetFov(), glm::radians(60.0f)) && Near(camera.GetNear(), 0.1f) && Near(camera.GetFar(), 1000.0f)));
  CHECK(NearMat(camera.GetViewMatrix(), Mat4(1.0f)));
  CHECK(NearMat(camera.GetProjectionMatrix(1.5f), glm::perspective(glm::radians(60.0f), 1.5f, 0.1f, 1000.0f)));
  CHECK(NearVec(camera.Forward(), {0, 0, -1}));
}

TEST_CASE("view matrix is the inverse world transform") {
  auto parent = std::make_shared<scene::Node3D>();
  auto camera = std::make_shared<Camera3D>();
  parent->AddChild(camera);
  parent->SetPosition({3, -2, 5});
  parent->SetEulerAngles({0.3f, -1.1f, 0.2f});
  camera->SetPosition({1, 2, 3});
  camera->LookAt({0, 0, 0});
  CHECK(NearMat(camera->GetViewMatrix(), glm::inverse(camera->GetWorldMatrix()), 1e-4f));
  CHECK(NearMat(camera->GetViewMatrix(), glm::lookAt(camera->GetWorldPosition(), Vec3(0), Vec3(0, 1, 0)), 1e-4f));

  // A scaled parent moves the camera but doesn't squash the picture: the view has no scale.
  parent->SetScale({2.0f, 0.5f, 3.0f});
  const Mat4 view = camera->GetViewMatrix();
  const Vec3 position = camera->GetWorldPosition();
  CHECK(NearVec(Vec3(view * Vec4(position, 1.0f)), {0, 0, 0}, 1e-4f));
  CHECK(NearVec(Vec3(view * Vec4(position + camera->Forward() * 4.0f, 1.0f)), {0, 0, -4}, 1e-3f));
  for (int i = 0; i < 3; ++i) CHECK(Near(glm::length(Vec3(view[i])), 1.0f, 1e-4f));
}

TEST_CASE("fit modes: orthographic") {
  Camera3D camera;
  camera.SetOrthographic(10.0f, 0.1f, 100.0f);
  camera.SetFrameAspect(2.0f);  // the frame: 20 x 10
  for (const float aspect : kAspects) {
    camera.SetFitMode(FitMode::KeepHeight);
    CHECK(Near(camera.VisibleSize(aspect).y, 10.0f));
    camera.SetFitMode(FitMode::KeepWidth);
    CHECK(Near(camera.VisibleSize(aspect).x, 20.0f));
    camera.SetFitMode(FitMode::Contain);  // all of the frame, one side exact
    Vec2 size = camera.VisibleSize(aspect);
    CHECK((size.x >= 20.0f - 1e-4f && size.y >= 10.0f - 1e-4f));
    CHECK((Near(size.x, 20.0f) || Near(size.y, 10.0f)));
    camera.SetFitMode(FitMode::Cover);  // inside the frame, one side exact
    size = camera.VisibleSize(aspect);
    CHECK((size.x <= 20.0f + 1e-4f && size.y <= 10.0f + 1e-4f));
    CHECK((Near(size.x, 20.0f) || Near(size.y, 10.0f)));
    CHECK(Near(size.x / size.y, aspect));
    // The projection shows exactly that: the visible corner lands on NDC (1, 1).
    const Vec3 corner = Ndc(camera.GetProjectionMatrix(aspect), {size.x / 2.0f, size.y / 2.0f, -5.0f});
    CHECK((Near(corner.x, 1.0f) && Near(corner.y, 1.0f)));
  }
}

TEST_CASE("fit modes: perspective") {
  Camera3D camera;
  camera.SetPerspective(glm::radians(50.0f), 0.1f, 100.0f);
  camera.SetFrameAspect(9.0f / 16.0f);
  const float distance = 7.0f;
  const float frame_height = 2.0f * std::tan(glm::radians(25.0f)) * distance;
  const float frame_width = frame_height * 9.0f / 16.0f;
  for (const float aspect : kAspects) {
    camera.SetFitMode(FitMode::KeepHeight);
    CHECK(Near(camera.VisibleSize(aspect, distance).y, frame_height, 1e-3f));
    camera.SetFitMode(FitMode::KeepWidth);
    CHECK(Near(camera.VisibleSize(aspect, distance).x, frame_width, 1e-3f));
    camera.SetFitMode(FitMode::Contain);
    const Vec2 size = camera.VisibleSize(aspect, distance);
    CHECK((size.x >= frame_width - 1e-3f && size.y >= frame_height - 1e-3f));
    CHECK((Near(size.x, frame_width, 1e-3f) || Near(size.y, frame_height, 1e-3f)));
    const Vec3 corner = Ndc(camera.GetProjectionMatrix(aspect), {size.x / 2.0f, size.y / 2.0f, -distance});
    CHECK((Near(corner.x, 1.0f, 1e-4f) && Near(corner.y, 1.0f, 1e-4f)));
    camera.SetFitMode(FitMode::Cover);
    const Vec2 covered = camera.VisibleSize(aspect, distance);
    CHECK((covered.x <= frame_width + 1e-3f && covered.y <= frame_height + 1e-3f));
  }
  // A bad aspect (a minimized window) is treated as square instead of making NaNs.
  CHECK(NearMat(camera.GetProjectionMatrix(0.0f), camera.GetProjectionMatrix(1.0f)));
}

TEST_CASE("frustum") {
  Camera3D camera;
  camera.SetPerspective(glm::radians(60.0f), 0.5f, 50.0f);
  camera.SetPosition({0, 0, 10});
  const Frustum frustum = camera.GetFrustum(1.0f);
  CHECK(Overlaps(frustum, AABB::FromCenterSize({0, 0, 0}, Vec3(1))));          // in front
  CHECK_FALSE(Overlaps(frustum, AABB::FromCenterSize({0, 0, 20}, Vec3(1))));   // behind
  CHECK_FALSE(Overlaps(frustum, AABB::FromCenterSize({0, 0, -50}, Vec3(1))));  // past far
  CHECK_FALSE(Overlaps(frustum, AABB::FromCenterSize({30, 0, 0}, Vec3(1))));   // to the side
  camera.RotateWorld({0, 1, 0}, glm::radians(-90.0f));                         // now looking down +x
  CHECK(Overlaps(camera.GetFrustum(1.0f), AABB::FromCenterSize({20, 0, 10}, Vec3(1))));
}

// ---- Controllers ----

TEST_CASE("fly controller clamps pitch and moves along its axes") {
  Camera3D camera;
  scene::FlyController fly;
  fly.damping = 0.0f;  // velocity = input at once
  fly.Attach(camera);
  fly.Update(camera, {.look = {0.0f, 10.0f}}, 0.016f);  // far past straight up
  CHECK(Near(fly.Pitch(), fly.pitch_limit));
  CHECK(camera.Forward().y < 1.0f);
  CHECK(camera.Forward().y > 0.99f);

  Camera3D level;
  scene::FlyController walk;
  walk.damping = 0.0f;
  walk.Attach(level);
  walk.Update(level, {.look = {glm::radians(90.0f), 0.0f}}, 0.0f);  // turn right: now facing +x
  CHECK(NearVec(level.Forward(), {1, 0, 0}, 1e-4f));
  walk.Update(level, {.move = {0, 0, 1}}, 0.5f);  // forward at 5 m/s for 0.5 s
  CHECK(NearVec(level.GetWorldPosition(), {2.5f, 0, 0}, 1e-4f));
  walk.Update(level, {.move = {0, 0, 1}, .boost = true}, 0.5f);
  CHECK(NearVec(level.GetWorldPosition(), {12.5f, 0, 0}, 1e-3f));

  // Damped: the speed builds up the same over one step or many.
  Camera3D a, b;
  scene::FlyController fa, fb;
  fa.Update(a, {.move = {1, 0, 0}}, 0.2f);
  for (int i = 0; i < 10; ++i) fb.Update(b, {.move = {1, 0, 0}}, 0.02f);
  CHECK(NearVec(fa.Velocity(), fb.Velocity(), 1e-4f));
}

TEST_CASE("orbit controller keeps its distance and frames a box") {
  Camera3D camera;
  scene::OrbitController orbit;
  orbit.pivot = {1, 2, 3};
  orbit.distance = 8.0f;
  orbit.Snap(camera);
  for (int i = 0; i < 20; ++i) {
    orbit.Update(camera, {.rotate = {0.3f, 0.1f}}, 0.016f);
    CHECK(Near(glm::distance(camera.GetWorldPosition(), Vec3(1, 2, 3)), 8.0f, 1e-3f));
    // It always looks at the pivot.
    CHECK(NearVec(camera.Forward(), glm::normalize(Vec3(1, 2, 3) - camera.GetWorldPosition()), 1e-3f));
  }
  orbit.Update(camera, {.rotate = {0.0f, 10.0f}}, 1.0f);  // pitch clamped
  CHECK(orbit.pitch >= -orbit.pitch_limit);

  // Frame: every corner of the box ends up on screen.
  const AABB box{{-3, -1, -2}, {5, 4, 1}};
  for (const float aspect : kAspects) {
    for (const bool ortho : {false, true}) {
      Camera3D view;
      if (ortho) view.SetOrthographic(1.0f, 0.1f, 1000.0f);
      scene::OrbitController framing;
      framing.yaw = 0.7f;
      framing.Frame(box, view, aspect);
      framing.Snap(view);
      const Mat4 view_projection = view.GetViewProjectionMatrix(aspect);
      for (int i = 0; i < 8; ++i) {
        const Vec3 corner((i & 1) ? box.max.x : box.min.x, (i & 2) ? box.max.y : box.min.y,
                          (i & 4) ? box.max.z : box.min.z);
        const Vec4 clip = view_projection * Vec4(corner, 1.0f);
        const Vec3 ndc = Vec3(clip) / clip.w;
        CHECK((std::abs(ndc.x) <= 1.0f && std::abs(ndc.y) <= 1.0f && ndc.z > -1.0f && ndc.z < 1.0f));
      }
    }
  }
}

TEST_CASE("follow controller lands in the same place with one step or ten") {
  auto target = std::make_shared<scene::Node3D>();
  target->SetPosition({4, 0, -2});
  target->SetEulerAngles({0.0f, 0.8f, 0.0f});
  scene::FollowController follow;
  follow.offset = {0, 3, 6};
  follow.offset_in_target_space = true;
  follow.position_lag = 5.0f;
  follow.rotation_lag = 3.0f;
  Camera3D one, ten;
  follow.Update(one, *target, 0.3f);
  for (int i = 0; i < 10; ++i) follow.Update(ten, *target, 0.03f);
  CHECK(NearVec(one.GetWorldPosition(), ten.GetWorldPosition(), 1e-4f));

  // Snap: right behind the target (its own axes), looking at it.
  follow.Snap(one, *target);
  CHECK(NearVec(one.GetWorldPosition(), target->GetWorldPosition() + target->GetWorldRotation() * Vec3(0, 3, 6)));
  CHECK(NearVec(one.Forward(), glm::normalize(target->GetWorldPosition() - one.GetWorldPosition()), 1e-4f));
  // After long enough it gets there with lag too.
  for (int i = 0; i < 200; ++i) follow.Update(ten, *target, 0.05f);
  CHECK(NearVec(ten.GetWorldPosition(), one.GetWorldPosition(), 1e-3f));
}

TEST_SUITE_END();
