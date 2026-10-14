#pragma once

#include <vector>

#include <glm/glm.hpp>

#include "wlw/core/collision.h"
#include "wlw/scene/node.h"

namespace wlw::scene {
class Scene;
class LightNode;
class SpriteBatch;
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
    scene::Node3D* node;
    scene::AABB box;  // world bounds, computed once per frame (GetAABB walks every mesh / copy)
  };
  struct Sprites {
    scene::SpriteBatch* batch;
    scene::AABB box;  // world bounds (its SetBounds box through its transform)
  };
  std::vector<const scene::LightNode*> lights;  // visible light nodes
  std::vector<Drawable> drawables;              // visible nodes with something to draw
  std::vector<Sprites> sprites;                 // visible sprite batches with dots (no shadows)
  scene::AABB casters{}, receivers{};           // around every shadow caster / receiver
  bool has_casters = false, has_receivers = false;

  void Clear();
};

// Visits every node of `scene` once: visible lights, drawables (with their world boxes), sprite
// batches, and the boxes around shadow casters and receivers. Appends to `out` (Clear it first).
void GatherScene(const scene::Scene& scene, SceneGather& out);

// What the camera sees, with what drawing it needs, worked out once per node.
struct VisibleSet {
  struct Node {
    scene::Node3D* node;
    glm::mat4 model;          // world matrix (walks the parents once)
    glm::mat3 normal_matrix;  // transpose(inverse(model)) for plain nodes; identity when instanced (per vertex)
    float depth;              // box centre's distance in front of the camera, along its view direction
  };
  struct Sprites {
    scene::SpriteBatch* batch;
    glm::mat4 model;
    float depth;
  };
  std::vector<Node> nodes;      // in gather order
  std::vector<Sprites> sprites;
  int culled = 0;               // drawables and sprite batches outside the frustum

  void Clear() {
    nodes.clear();
    sprites.clear();
    culled = 0;
  }
};

// Frustum-culls `gather`'s drawables and sprite batches against `view_projection` and fills `out`
// (cleared first). `view` gives the depths.
void CullScene(const SceneGather& gather, const glm::mat4& view, const glm::mat4& view_projection, VisibleSet& out);

}  // namespace wlw::rendering
