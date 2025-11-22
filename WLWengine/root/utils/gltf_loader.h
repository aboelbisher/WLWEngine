#pragma once

#include <memory>
#include <string>

#include "core/mesh.h"
#include "core/vertex_3d.h"
#include "core/model.h"

namespace wlw::utils {

class GLTFLoader {
public:
  static  std::shared_ptr<core::Model<core::Vertex3D>> LoadModel(const std::string& filename);
};

} // namespace wlw::utils
