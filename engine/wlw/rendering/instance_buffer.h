#pragma once

#include <glm/glm.hpp>

#include "wlw/core/color.h"

namespace wlw::rendering {

// One copy of a mesh in an instanced draw (see Node::SetInstances). `transform` is relative to the
// node; `tint` multiplies the mesh's vertex colours (white mesh + tint = any colour).
struct InstanceData {
  glm::mat4 transform{1.0f};
  core::Color tint{1.0f, 1.0f, 1.0f, 1.0f};
};

// The GPU copy of a node's instances. Created and filled by the renderer the first time the node
// is drawn, owned by the node (like a mesh owns its vertex buffer).
class WInstanceBuffer {
 public:
  virtual ~WInstanceBuffer() = default;
};

}  // namespace wlw::rendering
