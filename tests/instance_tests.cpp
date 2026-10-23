// Tests for instancing (Roadmap 1.6): rendering::Instance (52 bytes: layout, transforms, Color32),
// core::SinCos, TransformBox and InstanceBounds against brute-force corners, Node::WriteInstances
// and the node's cached instance bounds, plain nodes' boxes, and the deprecated shims.
#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <initializer_list>
#include <limits>
#include <memory>
#include <span>
#include <vector>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include "test_helpers.h"
#include "wlw/core/geometry.h"
#include "wlw/core/deprecated.h"
#include "wlw/core/math.h"
#include "wlw/core/model.h"
#include "wlw/core/random.h"
#include "wlw/fx/particle_system.h"
#include "wlw/rendering/instance_buffer.h"
#include "wlw/scene/node.h"

TEST_SUITE_BEGIN("instances");

using namespace wlw;
using rendering::Instance;

namespace {

// A random affine transform: rotation, scale (some axes mirrored or squashed), shear, translation.
glm::mat4 RandomAffine(core::Random& rng) {
  glm::mat4 m(1.0f);
  for (int column = 0; column < 3; ++column) {
    for (int row = 0; row < 3; ++row) m[column][row] = rng.Range(-2.0f, 2.0f);
  }
  m[3] = glm::vec4(rng.Range(-10.0f, 10.0f), rng.Range(-10.0f, 10.0f), rng.Range(-10.0f, 10.0f), 1.0f);
  return m;
}

AABB RandomBox(core::Random& rng) {
  const glm::vec3 a(rng.Range(-3.0f, 3.0f), rng.Range(-3.0f, 3.0f), rng.Range(-3.0f, 3.0f));
  const glm::vec3 size(rng.Range(0.0f, 2.0f), rng.Range(0.0f, 2.0f), rng.Range(0.0f, 2.0f));
  return {{a.x, a.y, a.z}, {a.x + size.x, a.y + size.y, a.z + size.z}};
}

// The box of the 8 transformed corners, the way GetAABB did it before 1.6.
void GrowByCorners(const glm::mat4& m, const AABB& box, glm::vec3& low, glm::vec3& high) {
  for (int i = 0; i < 8; ++i) {
    const glm::vec3 corner((i & 1) ? box.max.x : box.min.x, (i & 2) ? box.max.y : box.min.y,
                           (i & 4) ? box.max.z : box.min.z);
    const glm::vec3 p(m * glm::vec4(corner, 1.0f));
    low = glm::min(low, p);
    high = glm::max(high, p);
  }
}
AABB CornerBox(const glm::mat4& m, const AABB& box) {
  glm::vec3 low(std::numeric_limits<float>::max()), high(std::numeric_limits<float>::lowest());
  GrowByCorners(m, box, low, high);
  return {{low.x, low.y, low.z}, {high.x, high.y, high.z}};
}

bool SameBox(const AABB& a, const AABB& b, float tolerance = 1e-4f) {
  return Near(a.min.x, b.min.x, tolerance) && Near(a.min.y, b.min.y, tolerance) && Near(a.min.z, b.min.z, tolerance) &&
         Near(a.max.x, b.max.x, tolerance) && Near(a.max.y, b.max.y, tolerance) && Near(a.max.z, b.max.z, tolerance);
}

std::shared_ptr<core::Mesh<core::Vertex3D>> BoxMesh(const AABB& box) {
  std::vector<core::Vertex3D> vertices(2);
  vertices[0].position = box.min;
  vertices[1].position = box.max;
  auto mesh = std::make_shared<core::Mesh<core::Vertex3D>>();
  mesh->SetVertices(vertices);
  return mesh;
}

std::shared_ptr<core::Model<core::Vertex3D>> ModelOf(std::initializer_list<AABB> boxes) {
  auto model = std::make_shared<core::Model<core::Vertex3D>>();
  for (const AABB& box : boxes) model->meshes.push_back(BoxMesh(box));
  return model;
}

}  // namespace

TEST_CASE("instance layout and transform") {
  static_assert(sizeof(Instance) == 52);
  const Instance identity;
  CHECK(identity.Transform() == glm::mat4(1.0f));
  CHECK(identity.color == 0xFFFFFFFFu);

  core::Random rng(7);
  for (int i = 0; i < 100; ++i) {
    const glm::mat4 m = RandomAffine(rng);
    const Instance instance = Instance::FromTransform(m, {1.0f, 0.5f, 0.0f, 0.25f});
    CHECK(instance.Transform() == m);  // exact: the floats are only moved
    CHECK(instance.rows[1] == glm::vec4(m[0][1], m[1][1], m[2][1], m[3][1]));
    CHECK(instance.color == wlw::Color32(wlw::Color{1.0f, 0.5f, 0.0f, 0.25f}).Packed());
  }
  // Bytes r, g, b, a in memory, the order GL_UNSIGNED_BYTE x4 reads them.
  const Instance tinted = Instance::FromTransform(glm::mat4(1.0f), {1.0f, 0.0f, 0.2f, 0.6f});
  unsigned char bytes[4];
  std::memcpy(bytes, &tinted.color, 4);
  CHECK(bytes[0] == 255);
  CHECK(bytes[1] == 0);
  CHECK(bytes[2] == 51);
  CHECK(bytes[3] == 153);
}

TEST_CASE("unit to byte rounds like lround") {
  auto reference = [](float v) { return static_cast<uint32_t>(std::lround(std::clamp(v, 0.0f, 1.0f) * 255.0f)); };
  // Every 997th float in [0, 1] (the whole range was checked once, all 1,065,353,217).
  const uint32_t one = std::bit_cast<uint32_t>(1.0f);
  int mismatches = 0;
  for (uint32_t bits = 0; bits <= one; bits += 997) {
    const float v = std::bit_cast<float>(bits);
    if (UnitToByte(v) != reference(v)) ++mismatches;
  }
  CHECK(mismatches == 0);
  // Where truncating v * 255 + 0.5 would differ (0.5 / 255 rounds up), the halves around each byte,
  // the ends, outside, NaN.
  for (int k = 0; k < 255; ++k) {
    const float half = (static_cast<float>(k) + 0.5f) / 255.0f;
    for (float v : {half, std::nextafter(half, 0.0f), std::nextafter(half, 1.0f)}) {
      CHECK_MESSAGE(UnitToByte(v) == reference(v), v);
    }
  }
  CHECK(UnitToByte(0.5f / 255.0f) == 1);
  CHECK(UnitToByte(0.0f) == 0);
  CHECK(UnitToByte(1.0f) == 255);
  CHECK(UnitToByte(-3.0f) == 0);
  CHECK(UnitToByte(7.0f) == 255);
  CHECK(UnitToByte(std::numeric_limits<float>::quiet_NaN()) == 0);
  CHECK(Color32::FromPacked(Color32(Color{0.0f, 1.0f, 0.2f, 0.6f}).Packed()).ToColor().b == doctest::Approx(0.2f));
}

TEST_CASE("sincos matches the standard library") {
  double worst_near = 0.0, worst_far = 0.0;
  for (int i = -1000000; i <= 1000000; ++i) {
    const float x = static_cast<float>(i) * 0.001f;  // +-1000 rad
    const core::SinCosResult r = core::SinCos(x);
    const double error = std::max(std::abs(r.sin - std::sin(static_cast<double>(x))),
                                  std::abs(r.cos - std::cos(static_cast<double>(x))));
    double& worst = std::abs(x) <= 60.0f ? worst_near : worst_far;
    worst = std::max(worst, error);
  }
  CHECK(worst_near < 1.25e-7);
  CHECK(worst_far < 3e-7);
  // Quadrant edges: multiples of pi / 2 and the floats on both sides.
  for (int k = -200; k <= 200; ++k) {
    const float edge = static_cast<float>(k * 1.5707963267948966);
    for (float x : {edge, std::nextafter(edge, -1e9f), std::nextafter(edge, 1e9f)}) {
      const core::SinCosResult r = core::SinCos(x);
      CHECK_MESSAGE(std::abs(r.sin - std::sin(static_cast<double>(x))) < 2e-7, x);
      CHECK_MESSAGE(std::abs(r.cos - std::cos(static_cast<double>(x))) < 2e-7, x);
    }
  }
  CHECK(core::SinCos(0.0f).sin == 0.0f);
  CHECK(core::SinCos(0.0f).cos == 1.0f);
}

TEST_CASE("transform box matches the 8 corners") {
  core::Random rng(11);
  for (int i = 0; i < 200; ++i) {
    const glm::mat4 m = RandomAffine(rng);
    const AABB box = RandomBox(rng);
    CHECK(SameBox(TransformBox(m, box), CornerBox(m, box)));
  }
  // A flat box and a transform squashed to a plane stay finite.
  const AABB flat{{-1, 0, -1}, {1, 0, 1}};
  const AABB squashed = TransformBox(glm::scale(glm::mat4(1.0f), {2.0f, 0.0f, 1.0f}), flat);
  CHECK(SameBox(squashed, {{-2, 0, -1}, {2, 0, 1}}));
}

TEST_CASE("instance bounds match the corners of every copy") {
  core::Random rng(13);
  for (int round = 0; round < 20; ++round) {
    const AABB box = RandomBox(rng);
    std::vector<Instance> copies(1 + rng.Below(60));
    glm::vec3 low(std::numeric_limits<float>::max()), high(std::numeric_limits<float>::lowest());
    for (Instance& copy : copies) {
      const glm::mat4 m = RandomAffine(rng);
      copy.SetTransform(m);
      GrowByCorners(m, box, low, high);
    }
    CHECK(SameBox(rendering::InstanceBounds(copies, box), {{low.x, low.y, low.z}, {high.x, high.y, high.z}}));
  }
  CHECK(SameBox(rendering::InstanceBounds({}, {{1, 1, 1}, {2, 2, 2}}), {{0, 0, 0}, {0, 0, 0}}));
}

TEST_CASE("write instances keeps, adds and trims copies") {
  scene::Node3D node;
  CHECK_FALSE(node.IsInstanced());
  const uint64_t version = node.GetInstancesVersion();

  std::span<Instance> copies = node.WriteInstances(3);
  CHECK(node.IsInstanced());
  CHECK(node.InstanceCount() == 3);
  CHECK(node.GetInstancesVersion() == version + 1);
  for (const Instance& copy : copies) CHECK((copy.Transform() == glm::mat4(1.0f) && copy.color == 0xFFFFFFFFu));
  for (size_t i = 0; i < 3; ++i) copies[i].rows[0].w = static_cast<float>(i + 1);

  copies = node.WriteInstances(5);  // grows: the first 3 kept, new ones identity + white
  CHECK(node.GetInstancesVersion() == version + 2);
  CHECK(copies[2].rows[0].w == 3.0f);
  CHECK(copies[4].Transform() == glm::mat4(1.0f));

  copies = node.WriteInstances(2);  // trims
  CHECK(node.Instances().size() == 2);
  CHECK(node.Instances()[1].rows[0].w == 2.0f);

  node.ReserveInstances(100);
  const Instance* storage = node.WriteInstances(100).data();
  CHECK(node.WriteInstances(7).data() == storage);  // within the reserve: no reallocation
  CHECK(node.WriteInstances(100).data() == storage);

  node.WriteInstances(0);
  CHECK(node.IsInstanced());  // instanced with no copies: draws nothing
  node.ClearInstances();
  CHECK_FALSE(node.IsInstanced());
  CHECK(node.InstanceCount() == 0);
}

TEST_CASE("instance bounds are cached and follow the copies and the model") {
  scene::Node3D node;
  node.SetModel(ModelOf({{{-1, -1, -1}, {1, 1, 1}}, {{2, 0, 0}, {3, 1, 1}}}));  // model box (-1,-1,-1)..(3,1,1)
  node.SetPosition({5, 0, 0});
  std::span<Instance> copies = node.WriteInstances(2);
  copies[1].SetTransform(glm::translate(glm::mat4(1.0f), {0, 10, 0}));
  CHECK(SameBox(node.InstanceBounds(), {{-1, -1, -1}, {3, 11, 1}}));
  CHECK(SameBox(node.GetAABB(), {{4, -1, -1}, {8, 11, 1}}));  // + the node's position

  node.SetPosition({0, 0, 0});  // moving the node keeps the node-space bounds
  CHECK(SameBox(node.GetAABB(), {{-1, -1, -1}, {3, 11, 1}}));

  copies = node.WriteInstances(2);  // a write recomputes them
  copies[1].SetTransform(glm::scale(glm::mat4(1.0f), {2, 2, 2}));
  CHECK(SameBox(node.InstanceBounds(), {{-2, -2, -2}, {6, 2, 2}}));

  node.SetModel(ModelOf({{{0, 0, 0}, {1, 1, 1}}}));  // so does a new model...
  CHECK(SameBox(node.InstanceBounds(), {{0, 0, 0}, {2, 2, 2}}));
  node.GetModel()->meshes[0]->SetVertices({core::Vertex3D{}, core::Vertex3D{}});  // ...or new vertices
  CHECK(SameBox(node.InstanceBounds(), {{0, 0, 0}, {0, 0, 0}}));

  node.SetInstanceBounds(AABB{{-5, -5, -5}, {5, 5, 5}});  // the game's own box wins
  CHECK(SameBox(node.InstanceBounds(), {{-5, -5, -5}, {5, 5, 5}}));
  node.SetPosition({1, 0, 0});
  CHECK(SameBox(node.GetAABB(), {{-4, -5, -5}, {6, 5, 5}}));
  node.SetInstanceBounds(std::nullopt);  // computed again
  CHECK(SameBox(node.InstanceBounds(), {{0, 0, 0}, {0, 0, 0}}));

  node.WriteInstances(0);  // no copies: the small box at the node, as before
  CHECK(SameBox(node.GetAABB(), AABB::FromCenterSize({1, 0, 0}, Vec3(0.1f))));
}

TEST_CASE("instanced node box matches the corners of every copy in world space") {
  core::Random rng(17);
  scene::Node3D parent;
  auto node = std::make_shared<scene::Node3D>();
  parent.AddNode(node);
  parent.SetRotation({20, -35, 50});
  parent.SetScale({1.0f, -2.0f, 0.5f});
  node->SetPosition({1, 2, 3});
  node->SetRotation({10, 80, -30});
  const AABB box{{-0.5f, -0.2f, 0.0f}, {0.5f, 0.7f, 1.5f}};
  node->SetModel(ModelOf({box}));
  std::span<Instance> copies = node->WriteInstances(40);
  glm::vec3 low(std::numeric_limits<float>::max()), high(std::numeric_limits<float>::lowest());
  for (Instance& copy : copies) {
    const glm::mat4 m = RandomAffine(rng);
    copy.SetTransform(m);
    GrowByCorners(node->GetModelMatrix() * m, box, low, high);
  }
  // Two Arvo steps (per copy, then the node) can only be looser than the copies' own corners, never
  // tighter: it must contain them.
  const AABB aabb = node->GetAABB();
  CHECK((aabb.min.x <= low.x + 1e-3f && aabb.min.y <= low.y + 1e-3f && aabb.min.z <= low.z + 1e-3f));
  CHECK((aabb.max.x >= high.x - 1e-3f && aabb.max.y >= high.y - 1e-3f && aabb.max.z >= high.z - 1e-3f));
}

TEST_CASE("plain node box matches the corners of each mesh") {
  scene::Node3D parent;
  auto node = std::make_shared<scene::Node3D>();
  parent.AddNode(node);
  parent.SetPosition({3, -1, 2});
  parent.SetRotation({0, 0, 25});
  node->SetRotation({40, 15, 70});
  node->SetScale({1.5f, -0.5f, 2.0f});  // mirrored
  const AABB a{{-1, -1, -1}, {0, 0, 0}}, b{{2, 2, 2}, {3, 4, 3}};
  node->SetModel(ModelOf({a, b}));
  const glm::mat4 world = node->GetModelMatrix();
  glm::vec3 low(std::numeric_limits<float>::max()), high(std::numeric_limits<float>::lowest());
  GrowByCorners(world, a, low, high);
  GrowByCorners(world, b, low, high);
  CHECK(SameBox(node->GetAABB(), {{low.x, low.y, low.z}, {high.x, high.y, high.z}}));
}

TEST_CASE("model and material getters return the node's own pointers") {
  scene::Node3D node;
  auto model = ModelOf({{{0, 0, 0}, {1, 1, 1}}});
  node.SetModel(model);
  CHECK(node.GetModel() == model);
  CHECK(model.use_count() == 2);  // the getter took no copy
  const auto& held = node.GetModel();
  node.SetModel(nullptr);
  CHECK(held == nullptr);  // a held reference follows SetModel
}

#if !WLW_NO_DEPRECATED
WLW_DISABLE_DEPRECATION_WARNINGS_BEGIN
TEST_CASE("deprecated instance shims") {
  scene::Node3D node;
  std::vector<rendering::InstanceData> old(2);
  old[1].transform = glm::translate(glm::mat4(1.0f), {1, 2, 3});
  old[1].tint = {2.0f, 0.5f, -1.0f, 1.0f};  // tints are clamped to 0..1 now
  node.SetInstances(old);
  CHECK(node.InstanceCount() == 2);
  CHECK(node.Instances()[1].Transform() == old[1].transform);

  const std::vector<rendering::InstanceData> back = node.GetInstances();
  REQUIRE(back.size() == 2);
  CHECK(back[1].transform == old[1].transform);
  CHECK(back[1].tint.r == 1.0f);
  CHECK(Near(back[1].tint.g, 128.0f / 255.0f));
  CHECK(back[1].tint.b == 0.0f);

  node.SetInstances({});
  CHECK((node.IsInstanced() && node.InstanceCount() == 0));

  fx::ParticleSystem particles(64, 3u);
  fx::BurstSettings burst;
  burst.count_min = burst.count_max = 20;
  particles.Burst(burst, glm::vec3(1.0f, 2.0f, 3.0f));
  particles.Update(0.1f);
  std::vector<rendering::InstanceData> built;
  particles.BuildInstances(built);
  particles.WriteTo(node);
  REQUIRE(built.size() == node.InstanceCount());
  for (size_t i = 0; i < built.size(); ++i) {
    CHECK(built[i].transform == node.Instances()[i].Transform());
    CHECK(wlw::Color32(built[i].tint).Packed() == node.Instances()[i].color);
  }
}
WLW_DISABLE_DEPRECATION_WARNINGS_END
#endif

TEST_SUITE_END();
