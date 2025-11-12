#pragma once

#include <memory>
#include <vector>

#include "core/mesh.h"
#include "utils/texture.h"
#include "rendering/material.h"

namespace wlw::core {

  template <core::OnlyVerticesTypes T>
	class Model {
  public:
    std::vector<std::shared_ptr<Mesh<T>>> meshes;

    // We still keep a "Master List" to ensure textures are loaded 
    // and accessible even if no material currently uses them.
    std::vector<std::shared_ptr<utils::Texture>> textures;
    std::vector<std::shared_ptr<rendering::Material>> materials;

    std::string name;
	};


  using Mesh3DShared = std::shared_ptr<core::Mesh<core::Vertex3D>>;

} // wlw::core