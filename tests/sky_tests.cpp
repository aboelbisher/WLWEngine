// Tests for the sky (rendering/sky.h): the view rays of the full-screen triangle and where the sky
// goes in the draw order. CPU only.
#include <vector>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include "test_helpers.h"
#include "wlw/rendering/render_list.h"
#include "wlw/rendering/sky.h"

TEST_SUITE_BEGIN("sky");

using namespace wlw::rendering;

namespace {

bool NearVec(const glm::vec3& a, const glm::vec3& b, float tolerance = 1e-4f) {
  return Near(a.x, b.x, tolerance) && Near(a.y, b.y, tolerance) && Near(a.z, b.z, tolerance);
}

// The sky's ray through a point of the viewport (normalized device coordinates), as skybox.vert
// computes it.
glm::vec4 SkyRay(const glm::mat4& sky_ray, glm::vec2 ndc) { return sky_ray * glm::vec4(ndc, 1.0f, 1.0f); }

}  // namespace

TEST_CASE("perspective rays point through the pixels") {
  // A camera somewhere, looking somewhere: the ray through each point of the screen points from the
  // eye at what that point shows (unprojected at some depth), translation dropped.
  const glm::vec3 eye{3.0f, 2.0f, -5.0f};
  const glm::mat4 view = glm::lookAt(eye, glm::vec3{-1.0f, 0.5f, 2.0f}, glm::vec3{0, 1, 0});
  const glm::mat4 projection = glm::perspective(glm::radians(60.0f), 16.0f / 9.0f, 0.1f, 100.0f);
  const glm::mat4 sky_ray = SkyRayMatrix(view, projection);
  const glm::mat4 inverse_view_projection = glm::inverse(projection * view);
  for (const glm::vec2 ndc : {glm::vec2{0, 0}, glm::vec2{-1, -1}, glm::vec2{1, -1}, glm::vec2{-1, 1}, glm::vec2{3, -1},
                              glm::vec2{0.3f, -0.7f}}) {
    const glm::vec4 ray = SkyRay(sky_ray, ndc);
    CHECK(ray.w > 0.0f);  // the xyz alone point the right way
    const glm::vec4 point = inverse_view_projection * glm::vec4(ndc, 0.5f, 1.0f);
    const glm::vec3 expected = glm::normalize(glm::vec3(point) / point.w - eye);
    CHECK(NearVec(glm::normalize(glm::vec3(ray)), expected));
  }
  // The centre looks along the camera's forward axis.
  const glm::vec3 forward{-view[0][2], -view[1][2], -view[2][2]};
  CHECK(NearVec(glm::normalize(glm::vec3(SkyRay(sky_ray, {0, 0}))), forward));
}

TEST_CASE("rays interpolate linearly across the triangle") {
  // The triangle's corners carry their rays; the GPU interpolates them linearly across the screen:
  // the direction there must be the true one (the rays are linear in ndc on the far plane).
  const glm::mat4 view = glm::lookAt(glm::vec3{0, 1, 4}, glm::vec3{0, 0, 0}, glm::vec3{0, 1, 0});
  const glm::mat4 projection = glm::perspective(glm::radians(45.0f), 1.0f, 0.1f, 100.0f);
  const glm::mat4 sky_ray = SkyRayMatrix(view, projection);
  const glm::vec3 a = SkyRay(sky_ray, {-1, -1}), b = SkyRay(sky_ray, {3, -1}), c = SkyRay(sky_ray, {-1, 3});
  // (0.2, 0.4) = a + (b - a) * 0.3 + (c - a) * 0.35 in the corners' coordinates.
  const glm::vec3 interpolated = a + (b - a) * 0.3f + (c - a) * 0.35f;
  CHECK(NearVec(glm::normalize(interpolated), glm::normalize(glm::vec3(SkyRay(sky_ray, {0.2f, 0.4f})))));
}

TEST_CASE("the view's translation is dropped") {
  const glm::mat4 projection = glm::perspective(glm::radians(45.0f), 1.0f, 0.1f, 100.0f);
  const glm::mat4 here = glm::lookAt(glm::vec3{0, 0, 0}, glm::vec3{1, 0, 0}, glm::vec3{0, 1, 0});
  const glm::mat4 there = glm::lookAt(glm::vec3{50, -20, 7}, glm::vec3{51, -20, 7}, glm::vec3{0, 1, 0});
  const glm::vec3 ray_here = SkyRay(SkyRayMatrix(here, projection), {0.5f, 0.5f});
  const glm::vec3 ray_there = SkyRay(SkyRayMatrix(there, projection), {0.5f, 0.5f});
  CHECK(NearVec(glm::normalize(ray_here), glm::normalize(ray_there)));
}

TEST_CASE("orthographic rays cover the screen ahead") {
  // An orthographic camera gets a sky over the whole viewport too: every ray points ahead.
  const glm::mat4 view = glm::lookAt(glm::vec3{0, 10, 0.01f}, glm::vec3{0, 0, 0}, glm::vec3{0, 1, 0});
  const glm::mat4 projection = glm::ortho(-8.0f, 8.0f, -4.5f, 4.5f, 0.1f, 50.0f);
  const glm::mat4 sky_ray = SkyRayMatrix(view, projection);
  const glm::vec3 forward{-view[0][2], -view[1][2], -view[2][2]};
  for (const glm::vec2 ndc : {glm::vec2{0, 0}, glm::vec2{-1, -1}, glm::vec2{3, -1}, glm::vec2{-1, 3}}) {
    const glm::vec4 ray = SkyRay(sky_ray, ndc);
    CHECK((ray.w > 0.0f && glm::dot(glm::vec3(ray), forward) > 0.0f));
  }
  CHECK(NearVec(glm::normalize(glm::vec3(SkyRay(sky_ray, {0, 0}))), forward));
}

TEST_CASE("sky after the opaque draws") {
  std::vector<DrawOrder> order;
  for (uint32_t i = 0; i < 3; ++i) order.push_back({OpaqueSortKey(i, 0, 1.0f + i), i});
  for (uint32_t i = 0; i < 2; ++i) order.push_back({TransparentSortKey(1.0f + i, 0), 3 + i});
  SortDrawOrder(order);
  CHECK(SkyDrawPosition(order, /*has_depth=*/true) == 3);
  // Nothing to test depth against: first, under everything.
  CHECK(SkyDrawPosition(order, /*has_depth=*/false) == 0);
  // Only opaque draws: after all of them; only transparent ones, or none: first.
  std::vector<DrawOrder> opaque(order.begin(), order.begin() + 3);
  std::vector<DrawOrder> transparent(order.begin() + 3, order.end());
  CHECK(SkyDrawPosition(opaque, true) == 3);
  CHECK(SkyDrawPosition(transparent, true) == 0);
  CHECK(SkyDrawPosition({}, true) == 0);
}

TEST_SUITE_END();
