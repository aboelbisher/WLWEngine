
#pragma once

#include <memory>
#include <string>

#include "core/mesh.h"
#include "core/vertex_3d.h"

namespace wlw::utils {

class GLTFLoader {
public:
    static std::unique_ptr<core::Mesh<core::Vertex3D>> Load(const std::string& filepath);
};

} // namespace wlw::utils
