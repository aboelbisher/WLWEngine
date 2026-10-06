#include <glad/glad.h>
#include "texture.h"

namespace wlw::rendering {

void WTexture2D::Bind(unsigned int slot) const {
    glActiveTexture(GL_TEXTURE0 + slot);
    glBindTexture(GL_TEXTURE_2D, gpu_id_);
}

void WTexture2D::Unbind() const {
    glBindTexture(GL_TEXTURE_2D, 0);
}

void WCubemap::Bind(unsigned int slot) const {
    glActiveTexture(GL_TEXTURE0 + slot);
    glBindTexture(GL_TEXTURE_CUBE_MAP, gpu_id_);
}

void WCubemap::Unbind() const {
    glBindTexture(GL_TEXTURE_CUBE_MAP, 0);
}

} // namespace wlw::rendering
