#include "wlw/rendering/scene_gather.h"

#include <algorithm>

#include "wlw/core/normal_matrix.h"
#include "wlw/scene/camera_3d.h"
#include "wlw/scene/light.h"
#include "wlw/scene/scene.h"
#include "wlw/scene/sprite_batch.h"

namespace wlw::rendering {
namespace {

void Grow(AABB& box, bool& has_box, const AABB& add) {
  if (!has_box) {
    box = add;
    has_box = true;
    return;
  }
  box = Union(box, add);
}

// Distance of the box's centre in front of the camera along its view direction (right for both
// camera kinds).
float ViewDepth(const glm::mat4& view, const AABB& box) { return -(view * glm::vec4(box.Center(), 1.0f)).z; }

}  // namespace

void SceneGather::Clear() {
  lights.clear();
  drawables.clear();
  sprites.clear();
  has_casters = has_receivers = false;
}

namespace {

// What one visible node adds (its children not included).
void GatherOne(scene::Node3D& node, SceneGather& out) {
  if (scene::SpriteBatch* sprites = node.AsSpriteBatch()) {
    if (!sprites->Empty()) out.sprites.push_back({sprites, sprites->WorldBounds()});
    return;
  }
  if (const scene::LightNode* light = node.AsLight()) {
    out.lights.push_back(light);
    return;
  }
  const auto& model = node.GetModel();
  if (model == nullptr || model->meshes.empty()) return;
  if (node.IsInstanced() && node.InstanceCount() == 0) return;  // instanced with zero copies
  const AABB box = node.GetAABB();
  out.drawables.push_back({&node, box});
  if (node.CastsShadow()) Grow(out.casters, out.has_casters, box);
  if (node.ReceivesShadow()) Grow(out.receivers, out.has_receivers, box);
}

// A node, then its children: depth first in child order, a hidden node's whole branch skipped at once.
void GatherTree(scene::Node3D& node, SceneGather& out) {
  if (!node.IsVisible()) return;
  GatherOne(node, out);
  for (const std::shared_ptr<scene::Node3D>& child : node.GetChildren()) GatherTree(*child, out);
}

}  // namespace

void GatherScene(const scene::Scene& scene, SceneGather& out) {
  for (const std::shared_ptr<scene::Node3D>& node : scene.Root().GetChildren()) GatherTree(*node, out);
}

void CullScene(const SceneGather& gather, const glm::mat4& view, const glm::mat4& view_projection, VisibleSet& out,
               uint32_t cull_mask) {
  out.Clear();
  const Frustum frustum = Frustum::FromMatrix(view_projection);
  for (const SceneGather::Drawable& drawable : gather.drawables) {
    scene::Node3D* node = drawable.node;
    if ((node->GetLayers() & cull_mask) == 0) continue;  // not on the camera's layers: not counted as culled
    if (!Overlaps(frustum, drawable.box)) {
      ++out.culled;
      continue;
    }
    const glm::mat4& model = node->GetWorldMatrix();
    // Normals: the node's normal matrix, once per node (instanced copies add their own per vertex).
    out.nodes.push_back({node, model, core::NormalMatrix(model), ViewDepth(view, drawable.box)});
  }
  for (const SceneGather::Sprites& sprites : gather.sprites) {
    if ((sprites.batch->GetLayers() & cull_mask) == 0) continue;
    if (!Overlaps(frustum, sprites.box)) {
      ++out.culled;
      continue;
    }
    out.sprites.push_back({sprites.batch, sprites.batch->GetWorldMatrix(), ViewDepth(view, sprites.box)});
  }
}

}  // namespace wlw::rendering
