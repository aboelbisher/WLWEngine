#if WLW_DEBUG_UI
#include "wlw/debug/engine_panel.h"

#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>

#include "imgui.h"
#include "wlw/app/application.h"
#include "wlw/app/engine_cvars.h"
#include "wlw/core/build_info.h"
#include "wlw/core/frame_pacing.h"
#include "wlw/core/profile.h"

namespace wlw::debug {

void EnginePanel::OnDebugUI() {
  scene::Window& window = App().Window();
  ImGui::SetNextWindowCollapsed(true, ImGuiCond_FirstUseEver);
  ImGui::SetNextWindowPos(ImVec2(8.0f, 8.0f), ImGuiCond_FirstUseEver);
  ImGui::Begin("Engine");
  static const std::string build = core::BuildInfoText();  // never changes
  ImGui::TextDisabled("%s", build.c_str());
  ImGui::Text("%.0f fps, %.2f ms", App().Time().Fps(), App().Time().FrameMs());
  // Frame pacing (core/frame_pacing.h): the target, through the r.target_fps cvar like the console.
  ImGui::Text("Target");
  for (const int fps : {30, 60, 90, 120, core::kTargetDisplayRate, core::kTargetUncapped}) {
    ImGui::SameLine();
    const std::string label = core::TargetFpsName(fps);
    if (ImGui::RadioButton(label.c_str(), app::cv_target_fps.Get() == fps)) {
      app::cv_target_fps.Set(fps, CVarSource::Console);
    }
  }
  ImGui::Text("  paced to %.1f fps by %s; display %.1f Hz", window.PacedFps(), window.PacingMethod().c_str(),
              window.RefreshRate());
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
  ImGui::Text("Fragment mediump: %s", window.FragmentPrecision().c_str());  // r.mediump (glsl_header.h)
  bool invalidate = app::cv_invalidate_framebuffers.Get();                  // phones: compare the GPU time with it off
  if (ImGui::Checkbox("Invalidate framebuffers", &invalidate)) {
    app::cv_invalidate_framebuffers.Set(invalidate, CVarSource::Console);
  }
  // Render scale (Viewport::SetRenderScale): the screen's 3D resolution, through the r.render_scale
  // cvar like the console ("Auto" = 0: the game's or the quality level's).
  {
    const float forced = app::cv_render_scale.Get();
    ImGui::Text("Render scale");
    for (const float scale : {0.5f, 0.75f, 1.0f}) {
      ImGui::SameLine();
      char label[16];
      std::snprintf(label, sizeof(label), "%g", scale);
      if (ImGui::RadioButton(label, forced == scale)) app::cv_render_scale.Set(scale, CVarSource::Console);
    }
    ImGui::SameLine();
    if (ImGui::RadioButton("Auto##render_scale", forced <= 0.0f)) app::cv_render_scale.Set(0.0f, CVarSource::Console);
    const scene::Viewport& screen = window.MainViewport();
    const char* from = forced > 0.0f                          ? "r.render_scale"
                       : App().Quality().RenderScale() > 0.0f ? "the quality level"
                                                              : "the game";
    const scene::RenderStats& drawn = screen.GetRenderStats();
    ImGui::Text("  %.2f from %s: 3D %d x %d of %.0f x %.0f", screen.EffectiveRenderScale(), from, drawn.render_width,
                drawn.render_height, window.GetSize().x, window.GetSize().y);
    if (!window.ScreenHasDepth()) ImGui::TextDisabled("  screen without a depth buffer: 3D through its scene picture");
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
    ImGui::Text("  budget %.1f ms; reads GPU %.1f ms, frame %.1f ms; over %.1f s, under %.1f s (up after %.0f s)",
                quality.BudgetMs(), quality.SmoothedGpuMs(), quality.SmoothedFrameMs(), quality.SecondsOver(),
                quality.SecondsUnder(), quality.UpWait(quality.Level() + 1));
  }
  auto gpu_line = [](const char* name, const scene::Viewport& viewport) {
    const scene::GpuStats& gpu = viewport.GetGpuStats();
    if (gpu.scene_ms < 0.0f && gpu.canvas_ms < 0.0f) return;
    ImGui::Text("%s GPU: 3D %.2f ms, post %.2f ms, canvas %.2f ms", name, std::max(gpu.scene_ms, 0.0f),
                std::max(gpu.post_ms, 0.0f), std::max(gpu.canvas_ms, 0.0f));
  };
  gpu_line("Screen", window.MainViewport());
  const scene::RenderStats& stats = window.MainViewport().GetRenderStats();
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
    auto gpu_text = [&gpu_ms](size_t i, char (&text)[32]) {
      text[0] = '\0';
      if (i < gpu_ms.size() && gpu_ms[i] >= 0.0f) std::snprintf(text, sizeof(text), ", GPU %.2f ms", gpu_ms[i]);
    };
    char gpu[32];
    if (chain.Empty()) {  // a scaled viewport without passes: the copy that stretches its picture
      gpu_text(0, gpu);
      ImGui::BulletText("upscale (copy)%s", gpu);
    }
    for (size_t i = 0; i < chain.Passes().size(); ++i) {
      const auto& pass = chain.Passes()[i];
      const bool has_shader = pass.material && pass.material->GetShader();
      const std::string shader = has_shader ? pass.material->GetShader()->Name() : "copy";
      gpu_text(i, gpu);
      ImGui::BulletText("%s x%.2f%s%s%s", shader.c_str(), pass.scale, pass.name.empty() ? "" : " as ",
                        pass.name.c_str(), gpu);
    }
  };
  post_line("Screen", window.MainViewport());
  // Offscreen viewports: size, format, update mode, GPU memory of their texture, last frame's draws.
  for (const auto& viewport : window.Viewports()) {
    const rendering::RenderTarget* target = viewport->Internal().GetTarget();
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
    if (const scene::RenderStats& drawn = viewport->GetRenderStats(); viewport->EffectiveRenderScale() != 1.0f) {
      ImGui::Text("  render scale %.2f: 3D %d x %d", viewport->EffectiveRenderScale(), drawn.render_width,
                  drawn.render_height);
    }
    gpu_line(viewport->Settings().name.c_str(), *viewport);
    post_line(viewport->Settings().name.c_str(), *viewport);
  }
  const Vec2 framebuffer = window.GetSize();
  const Vec2 surface = window.GetInput().SurfaceSize();
  ImGui::Text("Framebuffer %.0f x %.0f px", framebuffer.x, framebuffer.y);
  ImGui::Text("Pointer area %.0f x %.0f%s", surface.x, surface.y, window.GetInput().IsTouchScreen() ? " (touch)" : "");
  ImGui::Text("Time scale x%.2f", App().Time().Scale());
  ImGui::Checkbox("ImGui demo window", &show_imgui_demo_);
  ImGui::End();
  if (show_imgui_demo_) ImGui::ShowDemoWindow(&show_imgui_demo_);  // every widget, with code
}

}  // namespace wlw::debug
#endif  // WLW_DEBUG_UI
