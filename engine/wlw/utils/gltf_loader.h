#pragma once

#include <memory>
#include <string>

#include "wlw/core/mesh.h"
#include "wlw/core/vertex_3d.h"
#include "wlw/core/model.h"
namespace wlw::utils {

class GLTFLoader {
public:
  static std::shared_ptr<core::Model<core::Vertex3D>> LoadModel(const std::string& filename, bool normalize = true);
};

} // namespace wlw::utils
