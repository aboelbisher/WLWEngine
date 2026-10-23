#pragma once

#include <memory>
#include <string>

#include "wlw/core/mesh.h"
#include "wlw/core/vertex_3d.h"
#include "wlw/core/model.h"
namespace wlw::utils {

// Meshes come out in the engine's axes (Z up, the model's front along -Y: kModelFront), turned from glTF's Y up.
class GLTFLoader {
 public:
  static std::unique_ptr<core::Model<core::Vertex3D>> LoadModel(const std::string& filename, bool normalize = true);
};

}  // namespace wlw::utils
