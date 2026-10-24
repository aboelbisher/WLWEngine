// Tests for wlw/core/geometry.h (Roadmap 2.1): the shapes' helpers, Overlaps and Raycast checked against brute
// force (points sampled along rays and inside shapes, fixed seeds), degenerate rays (zero direction components,
// origins on a face, parallel to a plane or triangle), the frustum tests never dropping a visible shape.

#include <algorithm>
#include <cmath>
#include <limits>
#include <optional>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include "test_helpers.h"
#include "wlw/core/geometry.h"
#include "wlw/core/random.h"
#include "wlw/scene/camera_3d.h"
#include "wlw/scene/node.h"
#include "wlw/scene/shadow_map.h"
#include "wlw/scene/sprite_batch.h"

TEST_SUITE_BEGIN("geometry");

using namespace wlw;

namespace {

Vec3 RandomPoint(core::Random& rng, float range) {
  return {rng.Range(-range, range), rng.Range(-range, range), rng.Range(-range, range)};
}
AABB RandomBox(core::Random& rng) {
  const Vec3 a = RandomPoint(rng, 3.0f);
  return {a, a + Vec3(rng.Range(0.1f, 3.0f), rng.Range(0.1f, 3.0f), rng.Range(0.1f, 3.0f))};
}
Ray RandomRay(core::Random& rng) { return {RandomPoint(rng, 8.0f), glm::normalize(RandomPoint(rng, 1.0f))}; }
// Every other ray aimed near `target` (random rays mostly miss small shapes).
Ray TestRay(core::Random& rng, int i, const Vec3& target) {
  if (i % 2 == 0) return RandomRay(rng);
  const Vec3 origin = RandomPoint(rng, 8.0f);
  return {origin, SafeNormalize(target + RandomPoint(rng, 1.0f) - origin, Vec3(0, 0, -1))};
}

// The first sample along the ray (steps of `step` up to `length`) that `inside` accepts, or nothing.
template <typename Inside>
std::optional<float> MarchRay(const Ray& ray, Inside inside, float step = 0.002f, float length = 30.0f) {
  for (float t = 0.0f; t <= length; t += step) {
    if (inside(ray.At(t))) return t;
  }
  return std::nullopt;
}

}  // namespace

TEST_CASE("box helpers") {
  const AABB box = AABB::FromCenterSize({1, 2, 3}, {2, 4, 6});
  CHECK((box.min == Vec3(0, 0, 0) && box.max == Vec3(2, 4, 6)));
  CHECK(box.Center() == Vec3(1, 2, 3));
  CHECK(box.Size() == Vec3(2, 4, 6));
  CHECK(box.Extents() == Vec3(1, 2, 3));
  CHECK(AABB::FromCenterExtents({1, 2, 3}, {1, 2, 3}).max == box.max);
  CHECK(box.Contains({2, 4, 6}));  // closed: the corner is in
  CHECK_FALSE(box.Contains({2.01f, 4, 6}));
  CHECK(box.Grown(1.0f).min == Vec3(-1, -1, -1));

  CHECK(AABB::Empty().IsEmpty());
  CHECK_FALSE(AABB{}.IsEmpty());  // the point box at the origin
  AABB grown = AABB::Empty();
  for (const Vec3& p : {Vec3(1, 5, -2), Vec3(-3, 0, 4), Vec3(0, 1, 0)}) grown = grown.Expanded(p);
  CHECK((grown.min == Vec3(-3, 0, -2) && grown.max == Vec3(1, 5, 4)));
  CHECK((Union(AABB::Empty(), box).min == box.min && Union(AABB::Empty(), box).max == box.max));
  const AABB both = Union(box, AABB{{-1, -1, -1}, {0, 0, 0}});
  CHECK((both.min == Vec3(-1, -1, -1) && both.max == box.max));
}

TEST_CASE("rect helpers") {
  const Rect r = Rect::FromPositionSize({10, 20}, {30, 40});
  CHECK((r.min == Vec2(10, 20) && r.max == Vec2(40, 60)));
  CHECK(r.Center() == Vec2(25, 40));
  CHECK(r.Size() == Vec2(30, 40));
  CHECK(Rect::FromCenterSize({25, 40}, {30, 40}).min == r.min);
  CHECK(r.Contains({40, 60}));
  CHECK_FALSE(r.Contains({41, 60}));
  CHECK(r.Grown(5.0f).min == Vec2(5, 15));
  CHECK(r.Expanded({0, 100}).min == Vec2(0, 20));
  CHECK(Overlaps(r, Rect{{40, 60}, {50, 70}}));  // touching corners
  CHECK_FALSE(Overlaps(r, Rect{{41, 0}, {50, 70}}));
  CHECK(Union(r, Rect{{0, 0}, {1, 1}}).min == Vec2(0, 0));
}

TEST_CASE("planes") {
  const Plane ground{{0, 1, 0}, 0};
  CHECK(ground.GetDistanceToPoint({3, 2, -1}) == 2.0f);
  const Plane raised = Plane::FromPointAndNormal({0, 2, 0}, {0, 1, 0});  // y = 2
  CHECK(raised.distance == -2.0f);
  const Plane through = Plane::FromPoints({0, 0, 0}, {1, 0, 0}, {0, 0, -1});  // counter-clockwise from +y
  CHECK((Near(through.normal.y, 1.0f) && Near(through.distance, 0.0f)));
  const Plane tilted = Plane::FromPoints({1, 0, 0}, {0, 1, 0}, {0, 0, 1});
  for (const Vec3& p : {Vec3(1, 0, 0), Vec3(0, 1, 0), Vec3(0, 0, 1)}) CHECK(Near(tilted.GetDistanceToPoint(p), 0.0f));
}

TEST_CASE("spheres") {
  const Sphere s{{1, 0, 0}, 2.0f};
  CHECK(s.Contains({3, 0, 0}));
  CHECK_FALSE(s.Contains({3.01f, 0, 0}));
  core::Random rng(3);
  for (int i = 0; i < 50; ++i) {
    const AABB box = RandomBox(rng);
    const Sphere around = BoundingSphere(box);
    for (int corner = 0; corner < 8; ++corner) {
      const Vec3 p((corner & 1) ? box.max.x : box.min.x, (corner & 2) ? box.max.y : box.min.y,
                   (corner & 4) ? box.max.z : box.min.z);
      CHECK(glm::distance(p, around.center) <= around.radius * 1.0001f);
    }
  }
}

TEST_CASE("overlaps match sampled points") {
  core::Random rng(17);
  for (int i = 0; i < 300; ++i) {
    const AABB a = RandomBox(rng), b = RandomBox(rng);
    const bool boxes = a.min.x <= b.max.x && b.min.x <= a.max.x && a.min.y <= b.max.y && b.min.y <= a.max.y &&
                       a.min.z <= b.max.z && b.min.z <= a.max.z;
    CHECK(Overlaps(a, b) == boxes);

    const Sphere s{RandomPoint(rng, 4.0f), rng.Range(0.1f, 2.0f)}, t{RandomPoint(rng, 4.0f), rng.Range(0.1f, 2.0f)};
    CHECK(Overlaps(s, t) == (glm::distance(s.center, t.center) <= s.radius + t.radius));

    // Box against sphere: the nearest point of a fine grid over the box.
    float nearest = std::numeric_limits<float>::max();
    constexpr int kSteps = 16;
    for (int x = 0; x <= kSteps; ++x) {
      for (int y = 0; y <= kSteps; ++y) {
        for (int z = 0; z <= kSteps; ++z) {
          const Vec3 p = a.min + a.Size() * Vec3(x, y, z) / static_cast<float>(kSteps);
          nearest = std::min(nearest, glm::distance(p, s.center));
        }
      }
    }
    const float grid_gap = glm::length(a.Size()) / kSteps;
    // The grid's nearest point is at most a grid gap further than the box's.
    if (nearest <= s.radius) CHECK(Overlaps(a, s));
    if (nearest > s.radius + grid_gap) CHECK_FALSE(Overlaps(a, s));
    CHECK(Overlaps(a, s) == Overlaps(s, a));
  }
}

TEST_CASE("raycast against boxes matches marching") {
  core::Random rng(23);
  int hits = 0;
  for (int i = 0; i < 300; ++i) {
    const AABB box = RandomBox(rng);
    const Ray ray = TestRay(rng, i, box.Center());
    const std::optional<float> t = Raycast(ray, box);
    const std::optional<float> marched = MarchRay(ray, [&](const Vec3& p) { return box.Contains(p); });
    CHECK(t.has_value() == marched.has_value());
    if (t && marched) {
      ++hits;
      CHECK(Near(*t, *marched, 0.003f));
      CHECK(box.Grown(1e-4f).Contains(ray.At(*t)));
    }
  }
  CHECK(hits > 50);  // the random cases do test hits
  // Starting inside: distance 0.
  CHECK(Raycast({{0.5f, 0.5f, 0.5f}, {0, 0, 1}}, AABB{{0, 0, 0}, {1, 1, 1}}) == 0.0f);
}

TEST_CASE("raycast against boxes with zero direction components") {
  const AABB box{{0, 0, 0}, {1, 1, 1}};
  CHECK(Raycast({{0.5f, 0.5f, -2.0f}, {0, 0, 1}}, box) == 2.0f);  // x and y components are 0
  CHECK(!Raycast({{1.5f, 0.5f, -2.0f}, {0, 0, 1}}, box));         // beside the box's x slab
  CHECK(Raycast({{1.0f, 0.5f, -2.0f}, {0, 0, 1}}, box) == 2.0f);  // on its face (closed)
  CHECK(Raycast({{0.0f, 0.0f, -2.0f}, {0, 0, 1}}, box) == 2.0f);  // along an edge
  CHECK(!Raycast({{0.5f, 0.5f, 2.0f}, {0, 0, 1}}, box));          // pointing away
  CHECK(Raycast({{-1.0f, 1.0f, 0.5f}, {1, 0, 0}}, box) == 1.0f);  // grazing the top face
  const float inf = std::numeric_limits<float>::infinity();
  CHECK(Raycast({{0.5f, 0.5f, -2.0f}, {0, 0, 1}}, AABB{{-inf, -inf, 0}, {inf, inf, 1}}) == 2.0f);  // a slab
}

TEST_CASE("raycast against spheres matches marching") {
  core::Random rng(29);
  int hits = 0;
  for (int i = 0; i < 300; ++i) {
    const Sphere sphere{RandomPoint(rng, 4.0f), rng.Range(0.2f, 3.0f)};
    const Ray ray = TestRay(rng, i, sphere.center);
    const std::optional<float> t = Raycast(ray, sphere);
    const std::optional<float> marched = MarchRay(ray, [&](const Vec3& p) { return sphere.Contains(p); });
    CHECK(t.has_value() == marched.has_value());
    if (t && marched) {
      ++hits;
      CHECK(Near(*t, *marched, 0.003f));
    }
  }
  CHECK(hits > 50);
  CHECK(Raycast({{0, 0, 0}, {1, 0, 0}}, Sphere{{0, 0, 0}, 1.0f}) == 0.0f);  // starts inside
  CHECK(Raycast({{-5, 0, 0}, {1, 0, 0}}, Sphere{{0, 0, 0}, 1.0f}) == 4.0f);
  CHECK(!Raycast({{-5, 2, 0}, {1, 0, 0}}, Sphere{{0, 0, 0}, 1.0f}));  // passes by
  CHECK(!Raycast({{5, 0, 0}, {1, 0, 0}}, Sphere{{0, 0, 0}, 1.0f}));   // behind
}

TEST_CASE("raycast against planes") {
  const Plane ground{{0, 1, 0}, 0};
  CHECK(!Raycast({{0, 1, 0}, {1, 0, 0}}, ground));  // parallel
  CHECK(!Raycast({{0, 1, 0}, {0, 1, 0}}, ground));  // pointing away
  CHECK(Raycast({{1, 5, 2}, {0, -1, 0}}, ground) == 5.0f);
  CHECK(Raycast({{0, -3, 0}, {0, 1, 0}}, ground) == 3.0f);  // from the back side
  const Ray slanted{{0, 2, 0}, glm::normalize(Vec3(1, -1, 0))};
  const std::optional<float> t = Raycast(slanted, ground);
  CHECK((t && Near(slanted.At(*t).y, 0.0f) && Near(slanted.At(*t).x, 2.0f)));
}

TEST_CASE("raycast against triangles") {
  const Vec3 a(0, 0, 0), b(2, 0, 0), c(0, 2, 0);
  CHECK(Raycast({{0.5f, 0.5f, 3}, {0, 0, -1}}, a, b, c) == 3.0f);
  CHECK(Raycast({{0.5f, 0.5f, -3}, {0, 0, 1}}, a, b, c) == 3.0f);  // both sides
  CHECK(!Raycast({{1.5f, 1.5f, 3}, {0, 0, -1}}, a, b, c));         // outside the hypotenuse
  CHECK(!Raycast({{0.5f, 0.5f, 3}, {0, 0, 1}}, a, b, c));          // behind
  CHECK(!Raycast({{0.5f, 0.5f, 0}, {1, 0, 0}}, a, b, c));          // in its plane
  CHECK(!Raycast({{0, 0, 1}, {0, 0, -1}}, a, a, c));               // degenerate triangle
  // Against the plane + inside test, for random triangles and rays.
  core::Random rng(31);
  int hits = 0;
  for (int i = 0; i < 500; ++i) {
    const Vec3 p0 = RandomPoint(rng, 3.0f), p1 = RandomPoint(rng, 3.0f), p2 = RandomPoint(rng, 3.0f);
    const Ray ray = TestRay(rng, i, (p0 + p1 + p2) / 3.0f);
    const Vec3 normal = glm::normalize(glm::cross(p1 - p0, p2 - p0));
    const std::optional<float> on_plane = Raycast(ray, Plane::FromPointAndNormal(p0, normal));
    bool inside = false;
    if (on_plane) {
      const Vec3 q = ray.At(*on_plane);
      // Same side of all three edges.
      const float e0 = glm::dot(glm::cross(p1 - p0, q - p0), normal);
      const float e1 = glm::dot(glm::cross(p2 - p1, q - p1), normal);
      const float e2 = glm::dot(glm::cross(p0 - p2, q - p2), normal);
      inside = e0 >= 0.0f && e1 >= 0.0f && e2 >= 0.0f;
      if (std::min({std::abs(e0), std::abs(e1), std::abs(e2)}) < 1e-3f) continue;  // too close to an edge to judge
    }
    const std::optional<float> t = Raycast(ray, p0, p1, p2);
    CHECK(t.has_value() == inside);
    if (t && on_plane) {
      ++hits;
      CHECK(Near(*t, *on_plane, 1e-3f));
    }
  }
  CHECK(hits > 50);
}

TEST_CASE("frustum tests never drop a visible shape") {
  const Mat4 projection = glm::perspective(glm::radians(60.0f), 1.5f, 0.5f, 40.0f);
  const Mat4 view = glm::lookAt(Vec3(1, 4, 9), Vec3(0, 0, 0), Vec3(0, 1, 0));
  const Mat4 view_projection = projection * view;
  const Frustum frustum = Frustum::FromMatrix(view_projection);
  auto visible = [&](const Vec3& p) {
    const Vec4 clip = view_projection * Vec4(p, 1.0f);
    return clip.w > 0.0f && std::abs(clip.x) <= clip.w && std::abs(clip.y) <= clip.w && std::abs(clip.z) <= clip.w;
  };
  // The planes face inwards: points on screen are on the positive side of all six.
  core::Random rng(37);
  for (int i = 0; i < 2000; ++i) {
    const Vec3 p = RandomPoint(rng, 30.0f);
    bool all_positive = true;
    for (const Plane& plane : frustum.planes) all_positive = all_positive && plane.GetDistanceToPoint(p) >= -1e-3f;
    if (visible(p)) CHECK(all_positive);
  }
  int culled = 0;
  for (int i = 0; i < 400; ++i) {
    const AABB box = AABB::FromCenterSize(RandomPoint(rng, 30.0f), Vec3(rng.Range(0.2f, 4.0f)));
    bool seen = false;
    for (int s = 0; s < 400 && !seen; ++s)
      seen = visible(box.min + box.Size() * Vec3(rng.Unit(), rng.Unit(), rng.Unit()));
    if (seen) CHECK(Overlaps(frustum, box));
    const Sphere sphere = BoundingSphere(box);
    if (seen) CHECK(Overlaps(frustum, sphere));
    if (!Overlaps(frustum, box)) ++culled;
  }
  CHECK(culled > 50);                                                         // and it does cull
  CHECK_FALSE(Overlaps(frustum, AABB::FromCenterSize({1, 4, 20}, Vec3(1))));  // behind the camera
  CHECK_FALSE(Overlaps(frustum, Sphere{{1, 4, 20}, 1.0f}));
  CHECK(Overlaps(frustum, Sphere{{0, 0, 0}, 0.1f}));
}

TEST_CASE("transform box keeps a flat box finite") {
  const AABB flat{{-1, 0, -1}, {1, 0, 1}};
  const AABB squashed = TransformBox(glm::scale(Mat4(1.0f), {2.0f, 0.0f, 1.0f}), flat);
  CHECK((squashed.min == Vec3(-2, 0, -1) && squashed.max == Vec3(2, 0, 1)));
}

TEST_SUITE_END();
