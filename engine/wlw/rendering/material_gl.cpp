#include "material.h"

namespace wlw::rendering {

  // Plain data: the renderer binds its texture (gl_resources.h).
  std::unique_ptr<Material> Material::Create() {
    return std::make_unique<Material>();
  }

} // namespace wlw::rendering
