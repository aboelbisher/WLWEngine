#pragma once

#include <cstdint>
#include <vector>

#include <glm/glm.hpp>

#include "wlw/core/geometry.h"
#include "wlw/scene/node.h"

namespace wlw::scene {
class Scene;
class GeometryInstance3D;
class Light3D;
class SpriteBatch;
class BillboardBatch;
}  // namespace wlw::scene

// The CPU half of drawing a viewport, with no GL in it: walk the scene once (GatherScene), then
// cull what the camera can't see and work out each visible node's matrices and depth (CullScene).
// The GL driver turns the visible set into draw items with programs and sort keys (render_list.h).
// Being GPU-free, it is benchmarked on its own (benchmarks/, "gather" / "cull").
// Game thread (or whichever thread draws).
namespace wlw::rendering {

// Everything a viewport's scene holds this frame. The lists are reused: Clear() keeps their memory.
struct SceneGather {
  struct Drawable {
    scene::GeometryInstance3D* node;
    AABB box;  // world bounds, computed once per frame (WorldBounds walks every mesh / copy)
  };
  struct Sprites {
    scene::SpriteBatch* batch;
    AABB box;  // world bounds (its SetBounds box through its transform)
  };
  struct Billboards {
    scene::BillboardBatch* batch;
    AABB box;  // world bounds (its SetBounds box through its transform)
  };
  std::vector<const scene::Light3D*> lights;  // visible light nodes
  std::vector<Drawable> drawables;            // visible mesh / multi-mesh nodes with something to draw
  std::vector<Sprites> sprites;               // visible sprite batches with dots (no shadows)
  std::vector<Billboards> billboards;         // visible billboard batches with quads (no shadows)
  AABB casters{}, receivers{};                // around every shadow caster / receiver
  bool has_casters = false, has_receivers = false;

  void Clear();
};

// Walks `scene` once (depth first, hidden branches skipped): visible lights, drawables (with their world boxes), sprite
// and billboard batches, and the boxes around shadow casters and receivers. Appends to `out` (Clear it first). The node
// types are told apart by Node3D::Kind() (no virtual call per node).
void GatherScene(const scene::Scene& scene, SceneGather& out);

// What the camera sees, with what drawing it needs, worked out once per node.
struct VisibleSet {
  struct Node {
    scene::GeometryInstance3D* node;
    glm::mat4 model;          // world matrix (Node3D::GetWorldMatrix, cached)
    glm::mat3 normal_matrix;  // core::NormalMatrix(model) (normal_matrix.h); instanced: the node's part
    float depth;              // box centre's distance in front of the camera, along its view direction
  };
  struct Sprites {
    scene::SpriteBatch* batch;
    glm::mat4 model;
    float depth;
  };
  struct Billboards {
    scene::BillboardBatch* batch;
    glm::mat4 model;
    float depth;
  };
  std::vector<Node> nodes;  // in gather order
  std::vector<Sprites> sprites;
  std::vector<Billboards> billboards;
  int culled = 0;  // drawables, sprite and billboard batches outside the frustum

  void Clear() {
    nodes.clear();
    sprites.clear();
    billboards.clear();
    culled = 0;
  }
};

// Frustum-culls `gather`'s drawables, sprite and billboard batches against `view_projection` and fills `out`
// (cleared first). `view` gives the depths. Nodes sharing no layer with `cull_mask` (the camera's,
// Camera3D::GetCullMask) are left out first (not counted in `culled`).
void CullScene(const SceneGather& gather, const glm::mat4& view, const glm::mat4& view_projection, VisibleSet& out,
               uint32_t cull_mask = 0xFFFFFFFFu);

}  // namespace wlw::rendering
