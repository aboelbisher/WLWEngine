#include "wlw/rendering/scene_gather.h"

#include <algorithm>

#include "wlw/core/normal_matrix.h"
#include "wlw/scene/camera_3d.h"
#include "wlw/scene/light.h"
#include "wlw/scene/scene.h"
#include "wlw/scene/sprite_batch.h"

namespace wlw::rendering {
namespace {

void Grow(scene::AABB& box, bool& has_box, const scene::AABB& add) {
  if (!has_box) {
    box = add;
    has_box = true;
    return;
  }
  box.min = {std::min(box.min.x, add.min.x), std::min(box.min.y, add.min.y), std::min(box.min.z, add.min.z)};
  box.max = {std::max(box.max.x, add.max.x), std::max(box.max.y, add.max.y), std::max(box.max.z, add.max.z)};
}

// Distance of the box's centre in front of the camera along its view direction (right for both
// camera kinds).
float ViewDepth(const glm::mat4& view, const scene::AABB& box) {
  const glm::vec3 center((box.min.x + box.max.x) * 0.5f, (box.min.y + box.max.y) * 0.5f,
                         (box.min.z + box.max.z) * 0.5f);
  return -(view * glm::vec4(center, 1.0f)).z;
}

}  // namespace

void SceneGather::Clear() {
  lights.clear();
  drawables.clear();
  sprites.clear();
  has_casters = has_receivers = false;
}

void GatherScene(const scene::Scene& scene, SceneGather& out) {
  scene.IterateOverNodes([&out](const std::shared_ptr<scene::Node3D>& node) {
    if (!node->IsVisibleInHierarchy()) return;
    if (scene::SpriteBatch* sprites = node->AsSpriteBatch()) {
      if (!sprites->Empty()) out.sprites.push_back({sprites, sprites->WorldBounds()});
      return;
    }
    if (const scene::LightNode* light = node->AsLight()) {
      out.lights.push_back(light);
      return;
    }
    const auto& model = node->GetModel();
    if (model == nullptr || model->meshes.empty()) return;
    if (node->IsInstanced() && node->GetInstances().empty()) return;  // instanced with zero copies
    const scene::AABB box = node->GetAABB();
    out.drawables.push_back({node.get(), box});
    if (node->CastsShadow()) Grow(out.casters, out.has_casters, box);
    if (node->ReceivesShadow()) Grow(out.receivers, out.has_receivers, box);
  });
}

void CullScene(const SceneGather& gather, const glm::mat4& view, const glm::mat4& view_projection, VisibleSet& out) {
  out.Clear();
  const scene::Frustum frustum = scene::Frustum::FromMatrix(view_projection);
  for (const SceneGather::Drawable& drawable : gather.drawables) {
    if (!frustum.TestAABB(drawable.box)) {
      ++out.culled;
      continue;
    }
    scene::Node3D* node = drawable.node;
    const glm::mat4 model = node->GetModelMatrix();
    // Normals: the node's normal matrix, once per node (instanced copies add their own per vertex).
    out.nodes.push_back({node, model, core::NormalMatrix(model), ViewDepth(view, drawable.box)});
  }
  for (const SceneGather::Sprites& sprites : gather.sprites) {
    if (!frustum.TestAABB(sprites.box)) {
      ++out.culled;
      continue;
    }
    out.sprites.push_back({sprites.batch, sprites.batch->GetModelMatrix(), ViewDepth(view, sprites.box)});
  }
}

}  // namespace wlw::rendering
