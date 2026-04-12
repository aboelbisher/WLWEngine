#include <glad/glad.h>
#include <GLFW/glfw3.h>

#include "material.h"
#include "core/logger.h"

namespace wlw::rendering {

  class MaterialGl : public Material {
  public:
    void BindTexture() const override {
      if (texture_) {
        texture_->Bind();
      }
    }

    void UnbindTexture() const override {
      if (texture_) {
        texture_->Unbind();
      }
    }

    ~MaterialGl() override {
    }
  };

  std::unique_ptr<Material> Material::Create() {
    return std::make_unique<MaterialGl>();
  }

} // namespace wlw::rendering
