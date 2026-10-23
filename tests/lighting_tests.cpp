// Tests for light nodes (scene/light.h) and the shadow-map fitting maths (scene/shadow_map.h).
// CPU only: what the renderer would draw from them isn't checked here.
#include <memory>

#include <glm/glm.hpp>

#include "test_helpers.h"
#include "wlw/core/deprecated.h"
#include "wlw/scene/geometry_instance.h"
#include "wlw/scene/light.h"
#include "wlw/scene/shadow_map.h"

TEST_SUITE_BEGIN("lighting");

using namespace wlw;

namespace {

bool NearVec(const glm::vec3& a, const glm::vec3& b, float tolerance = 1e-4f) {
  return glm::length(a - b) <= tolerance;
}

}  // namespace

TEST_CASE("lights are nodes") {
  auto sun = std::make_shared<scene::DirectionalLight3D>();
  auto bulb = std::make_shared<scene::PointLight3D>();
  scene::MeshInstance3D mesh;
  CHECK((sun->Kind() == scene::NodeKind::Light && bulb->Kind() == scene::NodeKind::Light &&
         mesh.Kind() == scene::NodeKind::MeshInstance && scene::Node3D().Kind() == scene::NodeKind::Node));
  CHECK((sun->GetType() == scene::Light3D::Type::Directional && bulb->GetType() == scene::Light3D::Type::Point));
  // Shadows are opt-in, for lights and for geometry.
  CHECK((!sun->CastsShadows() && !mesh.CastsShadow() && !mesh.ReceivesShadow()));
  // A directional light points straight down by default.
  CHECK(NearVec(sun->GetDirection(), {0.0f, -1.0f, 0.0f}));
}

TEST_CASE("set direction") {
  // SetDirection turns the node so its forward axis (-Z) points that way, for any direction.
  scene::DirectionalLight3D light;
  const glm::vec3 directions[] = {{-0.5f, -1.0f, -0.7f}, {0.0f, 0.0f, -1.0f}, {1.0f, 0.0f, 0.0f},
                                  {0.3f, 0.9f, 0.1f},    {0.0f, -1.0f, 0.0f}, {-1.0f, -1.0f, 1.0f}};
  for (const glm::vec3& d : directions) {
    light.SetDirection(d);
    CHECK(NearVec(light.GetDirection(), glm::normalize(d), 1e-3f));
  }
  // A parent's rotation turns the light with it: under a parent turned 90 degrees around Y,
  // a light pointing along -Z ends up pointing along -X.
  auto parent = std::make_shared<scene::Node3D>();
  auto child = std::make_shared<scene::DirectionalLight3D>();
  child->SetDirection({0.0f, 0.0f, -1.0f});
  parent->AddChild(child);
  parent->SetEulerAngles(glm::radians(wlw::Vec3(0.0f, 90.0f, 0.0f)), wlw::EulerOrder::XYZ);
  CHECK(NearVec(child->GetDirection(), {-1.0f, 0.0f, 0.0f}, 1e-3f));
}

TEST_CASE("spot light cone in radians") {
  scene::SpotLight3D spot;
  CHECK((Near(spot.GetInnerAngle(), glm::radians(12.5f)) && Near(spot.GetOuterAngle(), glm::radians(17.5f))));
  spot.SetInnerAngle(0.2f);
  spot.SetOuterAngle(0.3f);
  CHECK((spot.GetInnerAngle() == 0.2f && spot.GetOuterAngle() == 0.3f));
  CHECK(NearVec(spot.GetDirection(), {0.0f, -1.0f, 0.0f}));
}

#if !WLW_NO_DEPRECATED
WLW_DISABLE_DEPRECATION_WARNINGS_BEGIN
TEST_CASE("deprecated light names") {
  std::shared_ptr<scene::DirectionalLight> sun = std::make_shared<scene::DirectionalLight>();
  std::shared_ptr<scene::DirectionalLight3D> same = sun;  // the same type
  CHECK(same->GetType() == scene::Light3D::Type::Directional);
}
WLW_DISABLE_DEPRECATION_WARNINGS_END
#endif

TEST_CASE("shadow box contains the bounds") {
  // Every corner of the bounds lands inside the light's box (clip space -1..1 on all axes).
  const AABB bounds{{-4.0f, 0.0f, -5.0f}, {4.0f, 1.0f, 7.0f}};
  const glm::vec3 directions[] = {{-0.5f, -1.0f, -0.7f}, {0.0f, -1.0f, 0.0f}, {0.2f, -0.3f, 1.0f}};
  for (const glm::vec3& d : directions) {
    const scene::ShadowFrustum f = scene::FitDirectionalShadow(d, bounds, 1024);
    for (int i = 0; i < 8; ++i) {
      const glm::vec3 corner{(i & 1) ? bounds.max.x : bounds.min.x, (i & 2) ? bounds.max.y : bounds.min.y,
                             (i & 4) ? bounds.max.z : bounds.min.z};
      const glm::vec4 clip = f.ViewProjection() * glm::vec4(corner, 1.0f);
      CHECK((std::abs(clip.x) <= 1.0f && std::abs(clip.y) <= 1.0f && std::abs(clip.z) <= 1.0f));
    }
    // ...and tightly: never wider than the bounds' diagonal plus the half-unit rounding step.
    const glm::vec3 diagonal = glm::vec3(bounds.max) - glm::vec3(bounds.min);
    CHECK((f.texel_world > 0.0f && f.texel_world * 1024.0f <= glm::length(diagonal) + 0.5f));
  }
}

TEST_CASE("shadow box is stable") {
  // Looking straight down, a box 8 x 12 wide maps to a square of 12 (rounded up to half units).
  const AABB bounds{{-4.0f, 0.0f, -6.0f}, {4.0f, 1.0f, 6.0f}};
  const scene::ShadowFrustum f = scene::FitDirectionalShadow({0.0f, -1.0f, 0.0f}, bounds, 1024);
  CHECK(Near(f.texel_world, 12.0f / 1024.0f));
  // Moving the bounds by less than a texel keeps the box exactly where it was: no shimmer.
  AABB moved = bounds;
  moved.min.x += 0.001f;
  moved.max.x += 0.001f;
  const scene::ShadowFrustum g = scene::FitDirectionalShadow({0.0f, -1.0f, 0.0f}, moved, 1024);
  CHECK(g.ViewProjection() == f.ViewProjection());
  // Moving it by 10.5 texels moves the box by exactly 10 (whole texels only).
  moved = bounds;
  moved.min.x += 10.5f * f.texel_world;
  moved.max.x += 10.5f * f.texel_world;
  const scene::ShadowFrustum h = scene::FitDirectionalShadow({0.0f, -1.0f, 0.0f}, moved, 1024);
  const glm::vec4 a = f.ViewProjection() * glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);
  const glm::vec4 b = h.ViewProjection() * glm::vec4(10.0f * f.texel_world, 0.0f, 0.0f, 1.0f);
  CHECK((Near(a.x, b.x, 1e-3f) && Near(a.y, b.y, 1e-3f)));
}

TEST_CASE("big receiver does not stretch the map") {
  // Small casters over a huge floor: the map covers the casters (sharp shadows), and the floor
  // under them still fits in the box's depth.
  const AABB casters{{-2.0f, 0.0f, -2.0f}, {2.0f, 1.0f, 2.0f}};
  const AABB floor{{-50.0f, -0.1f, -50.0f}, {50.0f, 0.0f, 50.0f}};
  const glm::vec3 sun{-0.5f, -1.0f, -0.7f};
  const scene::ShadowFrustum f = scene::FitDirectionalShadow(sun, casters, floor, 1024);
  CHECK(f.texel_world * 1024.0f < 8.0f);  // about the casters' size, not the floor's 100
  // The floor point right under the casters' centre is inside the map, at a valid depth.
  const glm::vec4 clip = f.ViewProjection() * glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);
  CHECK((std::abs(clip.x) <= 1.0f && std::abs(clip.y) <= 1.0f && std::abs(clip.z) <= 1.0f));
  // So is the floor straight "behind" a caster corner along the light (where its shadow falls).
  const glm::vec3 shadow_spot =
      glm::vec3(2.0f, 1.0f, 2.0f) + glm::normalize(sun) * (1.0f / std::abs(glm::normalize(sun).y));
  const glm::vec4 spot = f.ViewProjection() * glm::vec4(shadow_spot, 1.0f);
  CHECK((std::abs(spot.x) <= 1.0f && std::abs(spot.y) <= 1.0f && std::abs(spot.z) <= 1.0f));
}

TEST_SUITE_END();
