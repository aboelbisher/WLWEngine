#if WLW_DEBUG_UI
#include "wlw/debug/engine_panel.h"

#include "imgui.h"
#include "wlw/app/application.h"

namespace wlw::debug {

void EnginePanel::OnDebugUI() {
  scene::Window& window = App().Window();
  ImGui::SetNextWindowCollapsed(true, ImGuiCond_FirstUseEver);
  ImGui::SetNextWindowPos(ImVec2(8.0f, 8.0f), ImGuiCond_FirstUseEver);
  ImGui::Begin("Engine");
  ImGui::Text("%.0f fps, %.2f ms", App().Time().Fps(), App().Time().FrameMs());
  const scene::RenderStats& stats = window.GetScreen().GetRenderStats();
  ImGui::Text("%d draw calls (%d transparent), %d instances, %d culled", stats.draw_calls,
              stats.transparent_draw_calls, stats.instances, stats.culled_nodes);
  ImGui::Text("%d program changes, %d texture binds, %d state calls skipped", stats.program_changes,
              stats.texture_binds, stats.state_calls_skipped);
  ImGui::Text("CPU: gather %.0f us, sort %.0f us, submit %.0f us", stats.gather_us, stats.sort_us, stats.submit_us);
  // Offscreen viewports: size, format, update mode, GPU memory of their texture, last frame's draws.
  for (const auto& [_, viewport] : window.GetViewports()) {
    const rendering::RenderTarget* target = viewport->GetTarget();
    if (target == nullptr) continue;
    static constexpr const char* kModes[] = {"always", "once", "disabled"};
    const rendering::TextureFormat format = target->Settings().format;
    const bool half_float = format == rendering::TextureFormat::RGBA16F || format == rendering::TextureFormat::R16F;
    const int texel_bytes = format == rendering::TextureFormat::RGBA16F ? 8
                            : format == rendering::TextureFormat::R16F  ? 2
                            : format == rendering::TextureFormat::R8    ? 1
                                                                        : 4;
    const float megabytes = static_cast<float>(target->Width()) * target->Height() *
                            (texel_bytes + (target->Settings().depth ? 4 : 0)) / (1024.0f * 1024.0f);
    ImGui::Text("%s: %d x %d %s, %s, %.1f MB, %d draws", viewport->Settings().name.c_str(), target->Width(),
                target->Height(), half_float ? "16F" : "8-bit", kModes[static_cast<int>(viewport->GetUpdateMode())],
                megabytes, viewport->GetRenderStats().draw_calls);
  }
  const core::Vector2 framebuffer = window.GetSize();
  const core::Vector2 surface = window.GetInput().SurfaceSize();
  ImGui::Text("Framebuffer %.0f x %.0f px", framebuffer.x, framebuffer.y);
  ImGui::Text("Pointer area %.0f x %.0f%s", surface.x, surface.y, window.GetInput().IsTouchScreen() ? " (touch)" : "");
  ImGui::Text("Time scale x%.2f", App().Time().Scale());
  ImGui::Checkbox("ImGui demo window", &show_imgui_demo_);
  ImGui::End();
  if (show_imgui_demo_) ImGui::ShowDemoWindow(&show_imgui_demo_);  // every widget, with code
}

}  // namespace wlw::debug
#endif  // WLW_DEBUG_UI
