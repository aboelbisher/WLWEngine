#include "wlw/scene/node.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <random>

#include "wlw/core/assert.h"

namespace wlw::scene {
namespace {

// SplitMix64's mixing step: consecutive inputs give unrelated outputs.
uint64_t Mix(uint64_t z) {
  z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ull;
  z = (z ^ (z >> 27)) * 0x94d049bb133111ebull;
  return z ^ (z >> 31);
}

// A fresh random id, never 0. Thread-safe: an atomic counter through the mix, seeded once per run (so two runs
// don't give the same ids: scene files from different sessions mix in one project).
uint64_t NewNodeId() {
  static const uint64_t seed = [] {
    std::random_device device;
    const uint64_t time = static_cast<uint64_t>(std::chrono::steady_clock::now().time_since_epoch().count());
    return (static_cast<uint64_t>(device()) << 32) ^ device() ^ Mix(time);
  }();
  static std::atomic<uint64_t> counter{0};
  for (;;) {
    const uint64_t id = Mix(seed + counter.fetch_add(1, std::memory_order_relaxed) * 0x9e3779b97f4a7c15ull);
    if (id != 0) return id;
  }
}

}  // namespace

Node3D::Node3D(NodeKind kind) : kind_(kind), id_(NewNodeId()) {}

Node3D::~Node3D() {
  for (const std::shared_ptr<Node3D>& child : children_) {
    if (child) child->parent_ = nullptr;
  }
}

// ---- Transform ----

void Node3D::SetRotation(const Quat& rotation) {
  rotation_ = glm::normalize(rotation);
  euler_set_ = false;
  MarkLocalDirty();
}

void Node3D::SetEulerAngles(const Vec3& radians, EulerOrder order) {
  rotation_ = QuatFromEuler(radians, order);
  euler_ = radians;
  euler_order_ = order;
  euler_set_ = true;
  MarkLocalDirty();
}

Vec3 Node3D::GetEulerAngles(EulerOrder order) const {
  if (euler_set_ && order == euler_order_) return euler_;
  return EulerFromQuat(rotation_, order);
}

void Node3D::Rotate(const Vec3& axis, float radians) {
  SetRotation(rotation_ * glm::angleAxis(radians, SafeNormalize(axis, kUp)));
}

void Node3D::RotateWorld(const Vec3& axis, float radians) {
  const Quat turn = glm::angleAxis(radians, SafeNormalize(axis, kUp));
  SetWorldRotation(turn * GetWorldRotation());
}

void Node3D::Translate(const Vec3& delta) { SetPosition(position_ + rotation_ * delta); }

void Node3D::TranslateWorld(const Vec3& delta) { SetWorldPosition(GetWorldPosition() + delta); }

void Node3D::LookAt(const Vec3& target, const Vec3& up) {
  const Vec3 direction = target - GetWorldPosition();
  if (glm::dot(direction, direction) < 1e-12f) return;
  SetWorldRotation(LookRotation(direction, up));
}

const Mat4& Node3D::GetLocalMatrix() const {
  if (local_dirty_) {
    // T * R * S without multiplying matrices: the rotation's columns scaled, then the position.
    const Mat3 r = glm::mat3_cast(rotation_);
    local_matrix_[0] = Vec4(r[0] * scale_.x, 0.0f);
    local_matrix_[1] = Vec4(r[1] * scale_.y, 0.0f);
    local_matrix_[2] = Vec4(r[2] * scale_.z, 0.0f);
    local_matrix_[3] = Vec4(position_, 1.0f);
    local_dirty_ = false;
  }
  return local_matrix_;
}

const Mat4& Node3D::GetWorldMatrix() const {
  if (world_dirty_) {
    world_matrix_ = parent_ ? parent_->GetWorldMatrix() * GetLocalMatrix() : GetLocalMatrix();
    world_dirty_ = false;
  }
  return world_matrix_;
}

Quat Node3D::GetWorldRotation() const { return parent_ ? parent_->GetWorldRotation() * rotation_ : rotation_; }

Vec3 Node3D::GetWorldScale() const { return parent_ ? parent_->GetWorldScale() * scale_ : scale_; }

void Node3D::SetWorldPosition(const Vec3& position) {
  SetPosition(parent_ ? Vec3(glm::inverse(parent_->GetWorldMatrix()) * Vec4(position, 1.0f)) : position);
}

void Node3D::SetWorldRotation(const Quat& rotation) {
  SetRotation(parent_ ? glm::inverse(parent_->GetWorldRotation()) * rotation : rotation);
}

Vec3 Node3D::Forward() const { return SafeNormalize(Vec3(GetWorldMatrix()[1]), kForward); }
Vec3 Node3D::Right() const { return SafeNormalize(Vec3(GetWorldMatrix()[0]), kRight); }
Vec3 Node3D::Up() const { return SafeNormalize(Vec3(GetWorldMatrix()[2]), kUp); }

void Node3D::MarkWorldDirty() {
  if (world_dirty_) return;  // the invariant: everything under it is marked already
  world_dirty_ = true;
  for (const std::shared_ptr<Node3D>& child : children_) child->MarkWorldDirty();
}

// ---- Hierarchy ----

bool Node3D::IsAncestorOf(const Node3D& node) const {
  for (const Node3D* n = node.parent_; n != nullptr; n = n->parent_) {
    if (n == this) return true;
  }
  return false;
}

void Node3D::AddChild(std::shared_ptr<Node3D> child) {
  if (!WLW_VERIFY(child != nullptr, "AddChild: null node")) return;
  if (!WLW_VERIFY(child.get() != this && !child->IsAncestorOf(*this),
                  "AddChild: a node can't be its own child or descendant")) {
    return;
  }
  if (child->parent_ != nullptr) child->parent_->DetachChildAt(child->GetIndex());
  child->parent_ = this;
  child->MarkWorldDirty();
  children_.push_back(std::move(child));
}

void Node3D::MoveChild(const Node3D* child, size_t index) {
  if (!WLW_VERIFY(child != nullptr && child->parent_ == this, "MoveChild: not a child of this node")) return;
  const size_t from = child->GetIndex();
  const size_t to = std::min(index, children_.size() - 1);
  if (from < to) {
    std::rotate(children_.begin() + static_cast<std::ptrdiff_t>(from),
                children_.begin() + static_cast<std::ptrdiff_t>(from) + 1,
                children_.begin() + static_cast<std::ptrdiff_t>(to) + 1);
  } else if (to < from) {
    std::rotate(children_.begin() + static_cast<std::ptrdiff_t>(to),
                children_.begin() + static_cast<std::ptrdiff_t>(from),
                children_.begin() + static_cast<std::ptrdiff_t>(from) + 1);
  }
}

void Node3D::DetachChildAt(size_t index) {
  Node3D& child = *children_[index];
  child.parent_ = nullptr;
  child.MarkWorldDirty();
  children_.erase(children_.begin() + static_cast<std::ptrdiff_t>(index));
}

std::shared_ptr<Node3D> Node3D::RemoveChild(const Node3D* child) {
  if (child == nullptr || child->parent_ != this) return nullptr;
  const size_t index = child->GetIndex();
  std::shared_ptr<Node3D> owned = children_[index];
  DetachChildAt(index);
  return owned;
}

std::shared_ptr<Node3D> Node3D::RemoveFromParent() { return parent_ ? parent_->RemoveChild(this) : nullptr; }

void Node3D::Reparent(Node3D& new_parent, bool keep_world) {
  if (!WLW_VERIFY(parent_ != nullptr, "Reparent: the node has no parent (use AddChild)")) return;
  if (!WLW_VERIFY(&new_parent != this && !IsAncestorOf(new_parent),
                  "Reparent: a node can't go under itself or its descendants")) {
    return;
  }
  const Vec3 world_position = GetWorldPosition();
  const Quat world_rotation = GetWorldRotation();
  const Vec3 world_scale = GetWorldScale();
  new_parent.AddChild(RemoveFromParent());
  if (keep_world) {
    SetWorldPosition(world_position);
    SetWorldRotation(world_rotation);
    const Vec3 parent_scale = new_parent.GetWorldScale();
    SetScale({parent_scale.x != 0.0f ? world_scale.x / parent_scale.x : scale_.x,
              parent_scale.y != 0.0f ? world_scale.y / parent_scale.y : scale_.y,
              parent_scale.z != 0.0f ? world_scale.z / parent_scale.z : scale_.z});
  }
}

size_t Node3D::GetIndex() const {
  if (parent_ == nullptr) return 0;
  const auto& siblings = parent_->children_;
  for (size_t i = 0; i < siblings.size(); ++i) {
    if (siblings[i].get() == this) return i;
  }
  WLW_UNREACHABLE("a node missing from its parent's children");
}

bool Node3D::IsVisibleInHierarchy() const {
  for (const Node3D* n = this; n != nullptr; n = n->parent_) {
    if (!n->visible_) return false;
  }
  return true;
}

// ---- Names and ids ----

Node3D* Node3D::FindChild(std::string_view name, bool recursive) const {
  for (const std::shared_ptr<Node3D>& child : children_) {
    if (child->name_ == name) return child.get();
    if (recursive) {
      if (Node3D* found = child->FindChild(name, true)) return found;
    }
  }
  return nullptr;
}

Node3D* Node3D::GetNode(std::string_view path) const {
  Node3D* node = const_cast<Node3D*>(this);
  while (node != nullptr && !path.empty()) {
    const size_t slash = path.find('/');
    const std::string_view step = path.substr(0, slash);
    path = slash == std::string_view::npos ? std::string_view() : path.substr(slash + 1);
    if (step.empty() || step == ".") continue;
    if (step == "..") {
      node = node->parent_;
      continue;
    }
    node = node->FindChild(step, false);
  }
  return node;
}

void Node3D::SetId(uint64_t id) {
  if (!WLW_VERIFY(id != 0, "SetId: 0 is not a valid node id")) return;
  id_ = id;
}

}  // namespace wlw::scene
