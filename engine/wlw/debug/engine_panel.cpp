#if WLW_DEBUG_UI
#include "wlw/debug/engine_panel.h"

#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>

#include "imgui.h"
#include "wlw/app/application.h"
#include "wlw/app/engine_cvars.h"
#include "wlw/core/profile.h"

namespace wlw::debug {

void EnginePanel::OnDebugUI() {
  scene::Window& window = App().Window();
  ImGui::SetNextWindowCollapsed(true, ImGuiCond_FirstUseEver);
  ImGui::SetNextWindowPos(ImVec2(8.0f, 8.0f), ImGuiCond_FirstUseEver);
  ImGui::Begin("Engine");
  ImGui::Text("%.0f fps, %.2f ms", App().Time().Fps(), App().Time().FrameMs());
  if constexpr (profile::kEnabled) {  // profile builds (core/profile.h)
    if (profile::Connected()) {
      ImGui::Text("Profiler: Tracy connected");
    } else {
      ImGui::TextDisabled("Profiler: waiting for Tracy (port 8086)");
    }
  }
  // The GPU's own time (it runs behind the CPU: these are a few frames old, smoothed).
  if (!window.GpuTimingSupported()) {
    ImGui::TextDisabled("GPU timing: not supported on this GPU");
  } else {
    bool timing = app::cv_gpu_timing.Get();
    if (ImGui::Checkbox("GPU timing", &timing)) app::cv_gpu_timing.Set(timing, CVarSource::Console);
    if (timing && window.GpuFrameMs() >= 0.0f) {
      ImGui::SameLine();
      ImGui::Text("GPU %.2f ms / frame", window.GpuFrameMs());
    }
  }
  bool invalidate = app::cv_invalidate_framebuffers.Get();  // phones: compare the GPU time with it off
  if (ImGui::Checkbox("Invalidate framebuffers", &invalidate)) {
    app::cv_invalidate_framebuffers.Set(invalidate, CVarSource::Console);
  }
  // Quality levels (app/quality.h): which one runs, what it reads, and a lock to try each.
  if (app::Quality& quality = App().Quality(); quality.Configured()) {
    ImGui::Text("Quality %d of 0..%d%s", quality.Level(), quality.Settings().levels - 1,
                quality.Locked() ? " (locked)" : "");
    ImGui::SameLine();
    // Through the quality.lock cvar, so the console and the CVars panel agree.
    if (ImGui::SmallButton("-")) app::cv_quality_lock.Set(std::max(quality.Level() - 1, 0), CVarSource::Console);
    ImGui::SameLine();
    if (ImGui::SmallButton("+")) {
      app::cv_quality_lock.Set(std::min(quality.Level() + 1, quality.Settings().levels - 1), CVarSource::Console);
    }
    if (quality.Locked()) {
      ImGui::SameLine();
      if (ImGui::SmallButton("Auto")) app::cv_quality_lock.Set(-1, CVarSource::Console);
    }
    ImGui::Text("  reads GPU %.1f ms, frame %.1f ms; over %.1f s, under %.1f s (up after %.0f s)",
                quality.SmoothedGpuMs(), quality.SmoothedFrameMs(), quality.SecondsOver(), quality.SecondsUnder(),
                quality.UpWait(quality.Level() + 1));
  }
  auto gpu_line = [](const char* name, const scene::Viewport& viewport) {
    const scene::GpuStats& gpu = viewport.GetGpuStats();
    if (gpu.scene_ms < 0.0f && gpu.canvas_ms < 0.0f) return;
    ImGui::Text("%s GPU: 3D %.2f ms, post %.2f ms, canvas %.2f ms", name, std::max(gpu.scene_ms, 0.0f),
                std::max(gpu.post_ms, 0.0f), std::max(gpu.canvas_ms, 0.0f));
  };
  gpu_line("Screen", window.GetScreen());
  const scene::RenderStats& stats = window.GetScreen().GetRenderStats();
  ImGui::Text("%d draw calls (%d transparent), %d instances (%.0f KB up), %d culled", stats.draw_calls,
              stats.transparent_draw_calls, stats.instances, stats.instance_bytes / 1024.0f, stats.culled_nodes);
  ImGui::Text("%d program changes, %d texture binds, %d state calls skipped", stats.program_changes,
              stats.texture_binds, stats.state_calls_skipped);
  ImGui::Text("CPU: gather %.0f us, sort %.0f us, submit %.0f us", stats.gather_us, stats.sort_us, stats.submit_us);
  auto post_line = [](const char* name, const scene::Viewport& viewport) {
    const scene::RenderStats& s = viewport.GetRenderStats();
    if (s.post_passes == 0) return;
    ImGui::Text("%s post: %d passes, %d textures, %d invalidated, CPU %.0f us", name, s.post_passes, s.post_targets,
                s.invalidations, s.post_us);
    const rendering::PostChain& chain = viewport.Post();
    const std::vector<float>& gpu_ms = viewport.GetGpuStats().pass_ms;
    for (size_t i = 0; i < chain.Passes().size(); ++i) {
      const auto& pass = chain.Passes()[i];
      const bool has_shader = pass.material && pass.material->GetShader();
      const std::string shader = has_shader ? pass.material->GetShader()->Name() : "copy";
      char gpu[32] = "";
      if (i < gpu_ms.size() && gpu_ms[i] >= 0.0f) std::snprintf(gpu, sizeof(gpu), ", GPU %.2f ms", gpu_ms[i]);
      ImGui::BulletText("%s x%.2f%s%s%s", shader.c_str(), pass.scale, pass.name.empty() ? "" : " as ", pass.name.c_str(),
                        gpu);
    }
  };
  post_line("Screen", window.GetScreen());
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
    gpu_line(viewport->Settings().name.c_str(), *viewport);
    post_line(viewport->Settings().name.c_str(), *viewport);
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
