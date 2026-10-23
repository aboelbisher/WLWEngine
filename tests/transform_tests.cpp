// Tests for Node3D's transform and hierarchy (Roadmap 2.2): Euler orders and look rotations
// (core/rotation.h), cached local/world matrices and their dirty marks, world getters and setters,
// children (order, moves, reparenting, refused loops), names and paths, ids, render layers, and the
// 0.3 shims (the old degrees rotation must give the old matrix).
#include <memory>
#include <unordered_set>
#include <vector>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include "test_helpers.h"
#include "wlw/core/deprecated.h"
#include "wlw/core/math.h"
#include "wlw/core/model.h"
#include "wlw/core/random.h"
#include "wlw/core/rotation.h"
#include "wlw/rendering/scene_gather.h"
#include "wlw/scene/geometry_instance.h"
#include "wlw/scene/light.h"
#include "wlw/scene/node.h"
#include "wlw/scene/scene.h"
#include "wlw/utils/primitives.h"

TEST_SUITE_BEGIN("transform");

using namespace wlw;
using scene::Node3D;

namespace {

bool NearVec(const Vec3& a, const Vec3& b, float tolerance = 1e-4f) {
  return Near(a.x, b.x, tolerance) && Near(a.y, b.y, tolerance) && Near(a.z, b.z, tolerance);
}
bool NearMat(const Mat4& a, const Mat4& b, float tolerance = 1e-4f) {
  for (int c = 0; c < 4; ++c) {
    for (int r = 0; r < 4; ++r) {
      if (!Near(a[c][r], b[c][r], tolerance)) return false;
    }
  }
  return true;
}
// Same rotation (q and -q are the same turn).
bool SameRotation(const Quat& a, const Quat& b, float tolerance = 1e-4f) {
  return Near(std::abs(glm::dot(a, b)), 1.0f, tolerance);
}

Mat4 Trs(const Vec3& t, const Quat& r, const Vec3& s) {
  return glm::translate(Mat4(1.0f), t) * glm::mat4_cast(r) * glm::scale(Mat4(1.0f), s);
}

// The 0.2 node's local matrix: T * Rx * Ry * Rz * S from degrees.
Mat4 OldLocalMatrix(const Vec3& position, const Vec3& degrees, const Vec3& scale) {
  const Vec3 radians = glm::radians(degrees);
  Mat4 m = glm::translate(Mat4(1.0f), position);
  m = glm::rotate(m, radians.x, Vec3(1.0f, 0.0f, 0.0f));
  m = glm::rotate(m, radians.y, Vec3(0.0f, 1.0f, 0.0f));
  m = glm::rotate(m, radians.z, Vec3(0.0f, 0.0f, 1.0f));
  return glm::scale(m, scale);
}

constexpr EulerOrder kOrders[] = {EulerOrder::XYZ, EulerOrder::XZY, EulerOrder::YXZ,
                                  EulerOrder::YZX, EulerOrder::ZXY, EulerOrder::ZYX};

std::shared_ptr<Node3D> Named(const char* name) {
  auto node = std::make_shared<Node3D>();
  node->SetName(name);
  return node;
}

}  // namespace

TEST_CASE("euler orders: the named product, and back") {
  const Vec3 angles(0.3f, -0.7f, 1.1f);
  const Quat x = glm::angleAxis(angles.x, Vec3(1, 0, 0));
  const Quat y = glm::angleAxis(angles.y, Vec3(0, 1, 0));
  const Quat z = glm::angleAxis(angles.z, Vec3(0, 0, 1));
  CHECK(SameRotation(QuatFromEuler(angles, EulerOrder::XYZ), x * y * z));
  CHECK(SameRotation(QuatFromEuler(angles, EulerOrder::XZY), x * z * y));
  CHECK(SameRotation(QuatFromEuler(angles, EulerOrder::YXZ), y * x * z));
  CHECK(SameRotation(QuatFromEuler(angles, EulerOrder::YZX), y * z * x));
  CHECK(SameRotation(QuatFromEuler(angles, EulerOrder::ZXY), z * x * y));
  CHECK(SameRotation(QuatFromEuler(angles, EulerOrder::ZYX), z * y * x));
  CHECK(SameRotation(QuatFromEuler(angles), z * x * y));  // ZXY by default: yaw (Z), pitch (X), roll (Y)

  // Random rotations survive quaternion -> angles -> quaternion in every order (the angles needn't match).
  core::Random rng(5);
  for (int i = 0; i < 200; ++i) {
    const Quat q = glm::normalize(
        Quat(rng.Range(-1.0f, 1.0f), rng.Range(-1.0f, 1.0f), rng.Range(-1.0f, 1.0f), rng.Range(-1.0f, 1.0f)));
    for (EulerOrder order : kOrders) {
      CHECK(SameRotation(QuatFromEuler(EulerFromQuat(q, order), order), q, 1e-3f));
    }
  }
  // Inside the ranges the angles come back as given.
  for (EulerOrder order : kOrders) CHECK(NearVec(EulerFromQuat(QuatFromEuler(angles, order), order), angles, 1e-4f));
}

TEST_CASE("look rotation: +Y to forward, +Z towards up, straight up and down") {
  const Vec3 forwards[] = {{1, 0, 0}, {0.3f, 2.0f, -0.5f}, {0, -1, 0}, {0, 0, -1}, {0, 0, 1}, {0, -0.0001f, -3}};
  for (const Vec3& f : forwards) {
    const Quat q = LookRotation(f);
    CHECK(Near(glm::length(q), 1.0f));
    CHECK(NearVec(q * kForward, glm::normalize(f), 1e-3f));
    CHECK(Near((q * kRight).z, 0.0f, 1e-3f));  // right stays level (no roll)
  }
  // Straight down: a pitch of -90 degrees, so +Z (the top of a camera's picture) goes to +Y.
  CHECK(NearVec(LookRotation(kDown) * kUp, kForward));
  CHECK(NearVec(LookRotation(kForward) * kUp, kUp));               // forward already: identity
  CHECK(SameRotation(LookRotation({0, 0, 0}), Quat(1, 0, 0, 0)));  // nothing to look along
}

TEST_CASE("local matrix is T * R * S") {
  Node3D node;
  CHECK(NearMat(node.GetLocalMatrix(), Mat4(1.0f)));
  CHECK(NearMat(node.GetWorldMatrix(), Mat4(1.0f)));
  const Quat r = QuatFromEuler({0.4f, 1.2f, -0.3f});
  node.SetPosition({1, 2, 3});
  node.SetRotation(r);
  node.SetScale({2.0f, -0.5f, 1.5f});
  CHECK(NearMat(node.GetLocalMatrix(), Trs({1, 2, 3}, r, {2.0f, -0.5f, 1.5f})));
  CHECK(NearMat(node.GetWorldMatrix(), node.GetLocalMatrix()));
  node.SetRotation(r * 3.0f);  // normalized when set
  CHECK(Near(glm::length(node.GetRotation()), 1.0f));
}

TEST_CASE("euler angles come back as set until the rotation changes another way") {
  Node3D node;
  const Vec3 set(glm::radians(190.0f), 0.2f, -0.1f);
  node.SetEulerAngles(set);
  CHECK(NearVec(node.GetEulerAngles(), set, 0.0f));  // exactly, not the equivalent (-170, ...)
  CHECK(SameRotation(QuatFromEuler(node.GetEulerAngles(EulerOrder::ZXY), EulerOrder::ZXY), node.GetRotation()));
  node.SetPosition({1, 0, 0});  // not a rotation change
  CHECK(NearVec(node.GetEulerAngles(), set, 0.0f));
  node.Rotate({0, 1, 0}, 0.5f);
  const Vec3 now = node.GetEulerAngles();
  CHECK(SameRotation(QuatFromEuler(now), node.GetRotation()));
  CHECK(now.x != set.x);  // worked out from the quaternion now
}

TEST_CASE("world matrix follows the parents, cached and marked dirty") {
  auto root = std::make_shared<Node3D>();
  auto middle = std::make_shared<Node3D>();
  auto leaf = std::make_shared<Node3D>();
  root->AddChild(middle);
  middle->AddChild(leaf);
  root->SetPosition({1, 0, 0});
  middle->SetRotation(glm::angleAxis(kHalfPi, Vec3(0, 1, 0)));
  middle->SetScale(Vec3(2.0f));
  leaf->SetPosition({0, 0, -1});
  auto expected = [&] { return root->GetLocalMatrix() * middle->GetLocalMatrix() * leaf->GetLocalMatrix(); };
  CHECK(NearMat(leaf->GetWorldMatrix(), expected()));
  CHECK(NearVec(leaf->GetWorldPosition(), {-1.0f, 0.0f, 0.0f}));  // (0,0,-1) x2, turned to -X, plus (1,0,0)

  // The cache: the same matrix object, updated after any ancestor changes (in either order of reads).
  const Mat4* cached = &leaf->GetWorldMatrix();
  CHECK(cached == &leaf->GetWorldMatrix());
  root->SetPosition({5, 0, 0});
  CHECK(NearMat(leaf->GetWorldMatrix(), expected()));
  middle->SetScale(Vec3(1.0f));
  CHECK(NearMat(middle->GetWorldMatrix(), root->GetLocalMatrix() * middle->GetLocalMatrix()));  // middle first
  CHECK(NearMat(leaf->GetWorldMatrix(), expected()));
  root->SetRotation(glm::angleAxis(0.3f, Vec3(1, 0, 0)));
  root->SetPosition({0, 1, 0});  // twice before a read: still right
  CHECK(NearMat(leaf->GetWorldMatrix(), expected()));

  // Moving to another parent dirties the moved branch.
  auto other = std::make_shared<Node3D>();
  other->SetPosition({0, 0, 10});
  (void)other->GetWorldMatrix();
  other->AddChild(middle);
  CHECK(NearMat(leaf->GetWorldMatrix(), other->GetLocalMatrix() * middle->GetLocalMatrix() * leaf->GetLocalMatrix()));
  middle->RemoveFromParent();
  CHECK(NearMat(leaf->GetWorldMatrix(), middle->GetLocalMatrix() * leaf->GetLocalMatrix()));
}

TEST_CASE("rotate, translate and look at, locally and in the world") {
  auto parent = std::make_shared<Node3D>();
  auto node = std::make_shared<Node3D>();
  parent->AddChild(node);
  parent->SetRotation(glm::angleAxis(kHalfPi, kUp));  // the parent's +Y (forward) is the world's -X
  parent->SetPosition({10, 0, 0});

  node->Translate(kForward);  // own axes = the parent's: one step along its +Y
  CHECK(NearVec(node->GetPosition(), {0, 1, 0}));
  CHECK(NearVec(node->GetWorldPosition(), {9, 0, 0}));
  node->TranslateWorld(kForward);  // world +Y
  CHECK(NearVec(node->GetWorldPosition(), {9, 1, 0}));

  node->Rotate(kUp, kHalfPi);  // its own Z: forward goes from world -X to world -Y
  CHECK(NearVec(node->Forward(), {0, -1, 0}));
  node->RotateWorld(kRight, kHalfPi);  // world X: -Y turns to -Z
  CHECK(NearVec(node->Forward(), {0, 0, -1}));
  CHECK(NearVec(node->Up(), glm::cross(node->Right(), node->Forward())));  // Z = X x Y

  node->LookAt({9, 1, 5});
  CHECK(NearVec(node->Forward(), {0, 0, 1}));  // straight up from where it is: the fallback up
  node->LookAt({20, 1, 0});
  CHECK(NearVec(node->Forward(), {1, 0, 0}));
  CHECK(NearVec(node->Up(), {0, 0, 1}));
  CHECK(SameRotation(node->GetWorldRotation(), parent->GetRotation() * node->GetRotation()));
  node->LookAt(node->GetWorldPosition());  // no direction: unchanged
  CHECK(NearVec(node->Forward(), {1, 0, 0}));
}

TEST_CASE("world setters under a scaled, turned parent") {
  auto parent = std::make_shared<Node3D>();
  auto node = std::make_shared<Node3D>();
  parent->AddChild(node);
  parent->SetPosition({1, 2, 3});
  parent->SetRotation(QuatFromEuler({0.2f, 0.9f, -0.4f}));
  parent->SetScale({2.0f, 2.0f, 2.0f});
  node->SetWorldPosition({-4, 5, 6});
  CHECK(NearVec(node->GetWorldPosition(), {-4, 5, 6}, 1e-3f));
  const Quat wanted = QuatFromEuler({-1.0f, 0.3f, 2.0f});
  node->SetWorldRotation(wanted);
  CHECK(SameRotation(node->GetWorldRotation(), wanted));
  node->SetScale({0.5f, 1.0f, 3.0f});
  CHECK(NearVec(node->GetWorldScale(), {1.0f, 2.0f, 6.0f}));
}

TEST_CASE("children: order, moves, removal, refused loops") {
  auto parent = Named("parent");
  auto a = Named("a"), b = Named("b"), c = Named("c");
  parent->AddChild(a);
  parent->AddChild(b);
  parent->AddChild(c);
  auto names = [&] {
    std::string out;
    for (const auto& child : parent->GetChildren()) out += child->GetName();
    return out;
  };
  CHECK(names() == "abc");
  CHECK((a->GetIndex() == 0 && c->GetIndex() == 2 && parent->ChildCount() == 3));
  parent->MoveChild(a.get(), 2);
  CHECK(names() == "bca");
  parent->MoveChild(a.get(), 0);
  CHECK(names() == "abc");
  parent->MoveChild(b.get(), 99);  // past the end = last
  CHECK(names() == "acb");

  CHECK(parent->RemoveChild(c.get()) == c);
  CHECK((names() == "ab" && c->GetParent() == nullptr));
  CHECK(parent->RemoveChild(c.get()) == nullptr);  // not a child any more
  CHECK(c->RemoveFromParent() == nullptr);

  // Adding takes the node from its old parent.
  c->AddChild(a);
  CHECK((names() == "b" && a->GetParent() == c.get()));

  // Loops are refused: a node under itself or under its own descendant.
  parent->AddChild(c);
  c->AddChild(parent);
  parent->AddChild(parent);
  a->AddChild(parent);
  CHECK((parent->GetParent() == nullptr && c->GetParent() == parent.get() && a->GetParent() == c.get()));
  parent->AddChild(nullptr);
  CHECK(parent->ChildCount() == 2);

  // A parent going away lets its children go.
  auto orphan = std::make_shared<Node3D>();
  {
    auto temporary = std::make_shared<Node3D>();
    temporary->AddChild(orphan);
  }
  CHECK(orphan->GetParent() == nullptr);
}

TEST_CASE("reparent keeps the world transform") {
  auto scene = scene::Scene::Create();
  auto from = std::make_shared<Node3D>(), to = std::make_shared<Node3D>(), node = std::make_shared<Node3D>();
  scene->Root().AddChild(from);
  scene->Root().AddChild(to);
  from->AddChild(node);
  from->SetPosition({1, 2, 3});
  from->SetRotation(QuatFromEuler({0.5f, -0.2f, 0.1f}));
  from->SetScale(Vec3(2.0f));
  to->SetPosition({-5, 0, 1});
  to->SetRotation(QuatFromEuler({-0.3f, 1.4f, 0.0f}));
  to->SetScale(Vec3(0.5f));
  node->SetPosition({0, 1, 0});
  node->SetRotation(QuatFromEuler({0.0f, 0.7f, 0.3f}));
  const Mat4 world = node->GetWorldMatrix();

  node->Reparent(*to);
  CHECK((node->GetParent() == to.get() && from->ChildCount() == 0));
  CHECK(NearMat(node->GetWorldMatrix(), world, 1e-3f));
  node->Reparent(scene->Root(), false);  // local kept: it moves
  CHECK(NearMat(node->GetWorldMatrix(), node->GetLocalMatrix()));
  node->Reparent(*node);  // refused
  CHECK(node->GetParent() == &scene->Root());
  auto free_node = std::make_shared<Node3D>();
  free_node->Reparent(*to);  // refused: no parent to take it from
  CHECK(free_node->GetParent() == nullptr);
}

TEST_CASE("names: find a child, paths") {
  auto root = Named("root");
  auto arm = Named("Arm"), hand = Named("Hand"), leg = Named("Leg"), hand2 = Named("Hand");
  root->AddChild(arm);
  arm->AddChild(hand);
  root->AddChild(leg);
  leg->AddChild(hand2);
  CHECK(root->FindChild("Hand") == hand.get());  // depth first: the arm's comes first
  CHECK(root->FindChild("Hand", false) == nullptr);
  CHECK(root->FindChild("Leg", false) == leg.get());
  CHECK(root->FindChild("Nose") == nullptr);
  CHECK(root->GetNode("Arm/Hand") == hand.get());
  CHECK(root->GetNode("Leg/Hand") == hand2.get());
  CHECK(hand->GetNode("../../Leg") == leg.get());
  CHECK(hand->GetNode("./..") == arm.get());
  CHECK(root->GetNode("") == root.get());
  CHECK(root->GetNode("Arm/Nose") == nullptr);
  CHECK(root->GetNode("..") == nullptr);
}

TEST_CASE("ids: random, never 0, settable") {
  std::unordered_set<uint64_t> ids;
  std::vector<std::unique_ptr<Node3D>> nodes;
  for (int i = 0; i < 10000; ++i) {
    nodes.push_back(std::make_unique<Node3D>());
    ids.insert(nodes.back()->GetId());
  }
  CHECK(ids.size() == nodes.size());
  CHECK(ids.count(0) == 0);
  Node3D node;
  node.SetId(42);
  CHECK(node.GetId() == 42);
  node.SetId(0);  // refused
  CHECK(node.GetId() == 42);
}

TEST_CASE("layers: the camera's cull mask") {
  auto model = std::make_shared<core::Model<core::Vertex3D>>();
  model->meshes.push_back(utils::CubeMesh(1.0f));
  scene::Scene scene;
  auto a = std::make_shared<scene::MeshInstance3D>(), b = std::make_shared<scene::MeshInstance3D>();
  a->SetModel(model);
  b->SetModel(model);
  b->SetLayers(1u << 3);
  CHECK(a->GetLayers() == 1u);
  scene.Root().AddChild(a);
  scene.Root().AddChild(b);
  rendering::SceneGather gather;
  rendering::GatherScene(scene, gather);
  REQUIRE(gather.drawables.size() == 2);  // layers don't change the gather (lights and shadows ignore them)
  const Mat4 view = glm::lookAt(Vec3(0, 0, 5), Vec3(0), Vec3(0, 1, 0));
  const Mat4 view_projection = glm::perspective(1.0f, 1.0f, 0.1f, 100.0f) * view;
  rendering::VisibleSet visible;
  rendering::CullScene(gather, view, view_projection, visible);
  CHECK(visible.nodes.size() == 2);
  rendering::CullScene(gather, view, view_projection, visible, 1u);
  CHECK((visible.nodes.size() == 1 && visible.nodes[0].node == a.get() && visible.culled == 0));
  rendering::CullScene(gather, view, view_projection, visible, (1u << 3) | (1u << 5));
  CHECK((visible.nodes.size() == 1 && visible.nodes[0].node == b.get()));
}

TEST_CASE("the gather skips hidden branches, depth first in child order") {
  auto model = std::make_shared<core::Model<core::Vertex3D>>();
  model->meshes.push_back(utils::CubeMesh(1.0f));
  scene::Scene scene;
  auto make = [&] {
    auto node = std::make_shared<scene::MeshInstance3D>();
    node->SetModel(model);
    return node;
  };
  auto first = make(), first_child = make(), hidden = make(), under_hidden = make(), last = make();
  scene.Root().AddChild(first);
  first->AddChild(first_child);
  scene.Root().AddChild(hidden);
  hidden->AddChild(under_hidden);
  hidden->SetVisible(false);
  scene.Root().AddChild(last);
  rendering::SceneGather gather;
  rendering::GatherScene(scene, gather);
  REQUIRE(gather.drawables.size() == 3);
  CHECK((gather.drawables[0].node == first.get() && gather.drawables[1].node == first_child.get() &&
         gather.drawables[2].node == last.get()));
  // Every node is still walked by ForEachNode (warm-up needs hidden pools).
  int count = 0;
  scene.ForEachNode([&](Node3D&) { ++count; });
  CHECK(count == 5);
}

#if !WLW_NO_DEPRECATED
WLW_DISABLE_DEPRECATION_WARNINGS_BEGIN
TEST_CASE("0.3 shims: degrees rotation, model matrix, AddNode") {
  core::Random rng(11);
  for (int i = 0; i < 100; ++i) {
    const Vec3 position(rng.Range(-5.0f, 5.0f), rng.Range(-5.0f, 5.0f), rng.Range(-5.0f, 5.0f));
    const Vec3 degrees(rng.Range(-400.0f, 400.0f), rng.Range(-400.0f, 400.0f), rng.Range(-400.0f, 400.0f));
    const Vec3 scale(rng.Range(-2.0f, 2.0f), rng.Range(0.1f, 2.0f), rng.Range(0.1f, 2.0f));
    Node3D node;
    node.SetPosition(position);
    node.SetRotation(degrees);
    node.SetScale(scale);
    CHECK(NearMat(node.GetLocalMatrix(), OldLocalMatrix(position, degrees, scale), 1e-4f));  // float rounding only
  }
  auto parent = std::make_shared<Node3D>();
  auto child = std::make_shared<Node3D>();
  parent->AddNode(child);
  parent->SetPosition({1, 0, 0});
  CHECK(child->GetParent() == parent.get());
  CHECK(NearMat(child->GetModelMatrix(), child->GetWorldMatrix()));

  scene::Scene scene;
  scene.AddNode(parent);
  int visited = 0;
  scene.IterateOverNodes([&](const std::shared_ptr<Node3D>&) { ++visited; });
  CHECK((scene.NodeCount() == 1 && visited == 2));
}
WLW_DISABLE_DEPRECATION_WARNINGS_END
#endif

TEST_CASE("lights aim with a look rotation") {
  scene::DirectionalLight3D light;
  CHECK(NearVec(light.GetDirection(), kDown));  // the default: straight down
  CHECK(NearVec(light.Up(), kForward));         // LookRotation's pitch of -90 degrees
  light.SetDirection(kUp);
  CHECK(NearVec(light.GetDirection(), kUp));
  light.SetDirection({3, 0, -4});
  CHECK(NearVec(light.GetDirection(), {0.6f, 0.0f, -0.8f}));
}

TEST_SUITE_END();
