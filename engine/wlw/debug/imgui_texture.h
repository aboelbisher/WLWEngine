#pragma once

#include "imgui.h"
#include "wlw/rendering/texture.h"

namespace wlw::debug {

// A texture for ImGui::Image / ImageButton, e.g. an offscreen viewport's picture or the Studio's Game panel
// (scene::OffscreenWindow::GetTexture). Made / uploaded on the GPU now if it wasn't yet. GL keeps a render target's
// row 0 at the BOTTOM: show those with kImGuiFlippedUv0 / kImGuiFlippedUv1. Inside an ImGui frame, game thread.
ImTextureRef ImGuiTexture(const rendering::WTexture& texture);

// The uv corners that show a render target's picture the right way up.
inline constexpr ImVec2 kImGuiFlippedUv0{0.0f, 1.0f};
inline constexpr ImVec2 kImGuiFlippedUv1{1.0f, 0.0f};

}  // namespace wlw::debug
