#include "wlw/debug/imgui_texture.h"

#include "wlw/rendering/gl_resources.h"

namespace wlw::debug {

ImTextureRef ImGuiTexture(const rendering::WTexture& texture) {
  // ImGui's OpenGL3 backend takes the GL texture name as the id.
  return ImTextureRef(static_cast<ImTextureID>(rendering::GLTexture(texture)));
}

}  // namespace wlw::debug
