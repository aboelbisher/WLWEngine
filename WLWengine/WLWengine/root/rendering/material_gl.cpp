
#include <glad/glad.h>
#include <GLFW/glfw3.h>

#include "material.h"
#include "core/logger.h"

namespace wlw::rendering {

  class MaterialGl : public Material {
  public:
    void BindTexture() const override {
      if (FAIL_IF(!texture_, "empty texture!")) {
        return;
      }
      glBindTexture(GL_TEXTURE_2D, texture_id_);
    }

    void UnbindTexture() const override {
      glBindTexture(GL_TEXTURE_2D, 0);
    }

    ~MaterialGl() override {
      if (texture_ == nullptr) {
        return;
      }
      UnbindTexture();
      //TODO: wtf is this ? find another way 
      GLuint textureIDs[1] = { texture_id_ };
      glDeleteTextures(1, textureIDs);
    }

    void GenerateTexture() override {
      if (FAIL_IF(!texture_, "empty texture!")) {
        return;
      }

      glGenTextures(1, &texture_id_);

      glBindTexture(GL_TEXTURE_2D, texture_id_);

      // 3. Set Texture Wrapping Parameters
      // S and T are the texture coordinates equivalent to X and Y
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);

      // 4. Set Texture Filtering Parameters
      // GL_LINEAR for smooth scaling, GL_NEAREST for blocky/pixelated look
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

      // Determine the format of the loaded data
      GLenum format = GL_RGB;
      auto channels_count = texture_->GetChannels();
      if (channels_count == 4) {
        format = GL_RGBA;
      } else if (channels_count == 1) {
        format = GL_RED;
      }

      auto texture_size = texture_->GetSize();

      // 5. Upload the Image Data to the Texture Object
      glTexImage2D(
        GL_TEXTURE_2D,      // Target: 2D texture
        0,                  // Mipmap level (0 is base level)
        format,             // Internal format: how OpenGL stores the data
        texture_size.x,              // Width of the texture
        texture_size.y,             // Height of the texture
        0,                  // Border (must be 0)
        format,             // Format of the source image data (e.g., GL_RGB, GL_RGBA)
        GL_UNSIGNED_BYTE,   // Data type of the source image data
        texture_->GetData()                // The actual image data
      );

      // 6. Generate Mipmaps (optional but recommended)
      // Mipmaps are smaller versions of the texture used for objects far away.
      glGenerateMipmap(GL_TEXTURE_2D);

      // 7. Unbind the texture (optional, good practice)
      glBindTexture(GL_TEXTURE_2D, 0);
    }

  private:
    GLuint texture_id_;

  };

  std::unique_ptr<Material> Material::Create() {
    return std::make_unique<MaterialGl>();
  }


} // namespace wlw::rendering