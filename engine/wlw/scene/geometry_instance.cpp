#include "wlw/scene/geometry_instance.h"

namespace wlw::scene {
namespace {

bool SameBox(const wlw::AABB& a, const wlw::AABB& b) { return a.min == b.min && a.max == b.max; }

}  // namespace

wlw::AABB GeometryInstance3D::ModelBox() const {
  if (!model_ || model_->meshes.empty()) return {{0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f}};
  wlw::AABB box = model_->meshes.front()->GetLocalAABB();
  for (size_t i = 1; i < model_->meshes.size(); ++i) box = wlw::Union(box, model_->meshes[i]->GetLocalAABB());
  return box;
}

bool GeometryInstance3D::HasGeometry() const {
  if (!model_ || model_->meshes.empty()) return false;
  return Kind() != NodeKind::MultiMeshInstance || static_cast<const MultiMeshInstance3D*>(this)->InstanceCount() > 0;
}

wlw::AABB GeometryInstance3D::LocalBounds() const {
  if (Kind() == NodeKind::MultiMeshInstance) return static_cast<const MultiMeshInstance3D*>(this)->InstanceBounds();
  return ModelBox();
}

wlw::AABB GeometryInstance3D::WorldBounds() const {
  const Mat4& world = GetWorldMatrix();
  if (!HasGeometry()) return wlw::AABB::FromCenterSize(Vec3(world[3]), Vec3(0.1f));
  if (Kind() == NodeKind::MultiMeshInstance) {
    return wlw::TransformBox(world, static_cast<const MultiMeshInstance3D*>(this)->InstanceBounds());
  }
  // Mesh by mesh: as tight as the 8 corners of each (a union box first would grow under rotation).
  wlw::AABB box = wlw::TransformBox(world, model_->meshes.front()->GetLocalAABB());
  for (size_t i = 1; i < model_->meshes.size(); ++i) {
    box = wlw::Union(box, wlw::TransformBox(world, model_->meshes[i]->GetLocalAABB()));
  }
  return box;
}

wlw::AABB MultiMeshInstance3D::InstanceBounds() const {
  if (custom_instance_bounds_) return *custom_instance_bounds_;
  const wlw::AABB model_box = ModelBox();
  if (instance_bounds_version_ != instances_version_ || !SameBox(instance_bounds_model_box_, model_box)) {
    instance_bounds_ = rendering::InstanceBounds(instances_, model_box);
    instance_bounds_model_box_ = model_box;
    instance_bounds_version_ = instances_version_;
  }
  return instance_bounds_;
}

}  // namespace wlw::scene
