#pragma once

#include <algorithm>
#include <cmath>
#include <memory>
#include <optional>
#include <string>

#include <glm/glm.hpp>

#include "wlw/core/color.h"
#include "wlw/rendering/render_device.h"
#include "wlw/rendering/render_target.h"
#include "wlw/scene/camera_3d.h"
#include "wlw/scene/fps_camera.h"
#include "wlw/scene/scene.h"
#include "wlw/ui/canvas.h"

namespace wlw::scene {

// What the renderer did for a viewport's last drawn frame (shown in the debug UI).
struct RenderStats {
  int draw_calls = 0;    // one per mesh drawn; an instanced node is one call for all its copies
  int transparent_draw_calls = 0;  // of those, drawn in the transparent queue (blending, back to front)
  int instances = 0;     // copies drawn by instanced calls
  int culled_nodes = 0;  // skipped by frustum culling
};

struct ViewportSettings {
  std::string name = "Viewport";                              // shown in the debug UI
  rendering::TextureFormat format = rendering::TextureFormat::RGBA8;  // RGBA16F: glow, values above 1
  bool depth = true;    // a depth buffer; false for a 2D-only viewport (saves memory)
  bool smooth = true;   // linear filtering when its texture is sampled
  // Each drawn frame starts from this colour; nullopt keeps the last picture (trails, painting).
  std::optional<core::Color> clear = core::Color{0.0f, 0.0f, 0.0f, 0.0f};
};

// "Draw this scene, from this camera, into this picture" - like Godot's SubViewport. It shows a
// Scene (which it only points at: several viewports may share one), through a camera, with a 2D
// Canvas drawn over the 3D. The window's own viewport is the screen (Window::GetScreen()); the
// others draw into a texture before the screen, in the order they were added
// (Window::AddViewport), so a viewport may use the texture of one added before it.
//
//   auto view = scene::Viewport::Create(device, {.format = TextureFormat::RGBA16F, .depth = false});
//   view->SetSizeFromScreen(0.5f);          // half resolution, follows rotation / resizes
//   view->SetCamera(top_down_camera);
//   view->GetScene()->AddNode(particles);   // its own scene; or SetScene(shared_world)
//   window.AddViewport(view);
//   material->SetParam("uPicture", view->GetTexture());
class Viewport {
 public:
  enum class UpdateMode {
    Always,    // drawn every frame
    Once,      // drawn on the next frame, then kept (static content: costs nothing afterwards)
    Disabled,  // not drawn; the texture keeps its last picture (redrawn once if a resize lost it)
  };

  // An offscreen viewport with its own (empty) scene, drawing into a texture made by `device`.
  static std::shared_ptr<Viewport> Create(rendering::RenderDevice& device, const ViewportSettings& settings = {}) {
    rendering::RenderTargetSettings target{settings.format, settings.smooth, settings.depth};
    return std::make_shared<Viewport>(device.CreateRenderTarget(1, 1, target), settings);
  }

  // `target` null = draws to the window (the screen). Games use Create().
  Viewport(std::shared_ptr<rendering::RenderTarget> target, const ViewportSettings& settings)
      : target_(std::move(target)), settings_(settings) {}

  // ---- What it shows ----
  void SetScene(std::shared_ptr<Scene> scene) { scene_ = std::move(scene); }  // null: no 3D, only the canvas
  const std::shared_ptr<Scene>& GetScene() const { return scene_; }
  void SetCamera(std::shared_ptr<Camera3D> camera) { camera_ = std::move(camera); }
  const std::shared_ptr<Camera3D>& GetCamera() const { return camera_; }
  // 2D drawn over its 3D: emptied and sized to the viewport every frame (in the app's UI units,
  // so a screen-proportioned viewport has the same Size() as the screen's canvas); draw into it
  // during OnRenderUI.
  ui::Canvas& GetCanvas() { return canvas_; }
  const ui::Canvas& GetCanvas() const { return canvas_; }

  // ---- Its picture ----
  // The texture it draws into (null for the screen). The same object for the viewport's life.
  std::shared_ptr<rendering::WTexture2D> GetTexture() const { return target_ ? target_->GetTexture() : nullptr; }
  rendering::RenderTarget* GetTarget() const { return target_.get(); }
  bool IsScreen() const { return target_ == nullptr; }

  // Size: fixed pixels, or a fraction of the screen that follows it (the default: 1.0).
  void SetSize(int width, int height) {
    fixed_size_ = {std::max(width, 1), std::max(height, 1)};
    screen_scale_.reset();
  }
  void SetSizeFromScreen(float scale) {
    screen_scale_ = std::max(scale, 0.0f);
    fixed_size_.reset();
  }
  // Its size in pixels for a screen of `screen_pixels`; {0, 0} while the screen has no size
  // (minimized), when a screen-sized viewport isn't drawn.
  glm::ivec2 PixelSize(glm::vec2 screen_pixels) const {
    if (fixed_size_) return *fixed_size_;
    if (screen_pixels.x <= 0.0f || screen_pixels.y <= 0.0f) return {0, 0};
    const float scale = screen_scale_.value_or(1.0f);
    return {std::max(1, static_cast<int>(std::lround(screen_pixels.x * scale))),
            std::max(1, static_cast<int>(std::lround(screen_pixels.y * scale)))};
  }

  void SetUpdateMode(UpdateMode mode) { update_mode_ = mode; }
  UpdateMode GetUpdateMode() const { return update_mode_; }
  void SetClearColor(std::optional<core::Color> color) { settings_.clear = color; }
  const std::optional<core::Color>& GetClearColor() const { return settings_.clear; }
  const ViewportSettings& Settings() const { return settings_; }

  const RenderStats& GetRenderStats() const { return render_stats_; }

  // ---- For the renderer ----
  // Whether to draw it this frame, at `pixel_size`: applies the update mode (Once becomes
  // Disabled), and redraws a kept picture once after a resize threw it away.
  bool BeginDraw(glm::ivec2 pixel_size) {
    if (pixel_size.x <= 0 || pixel_size.y <= 0) return false;
    const bool resized = pixel_size != last_size_;
    last_size_ = pixel_size;
    const bool draw = update_mode_ != UpdateMode::Disabled || (resized && has_picture_);
    if (update_mode_ == UpdateMode::Once) update_mode_ = UpdateMode::Disabled;
    if (draw) has_picture_ = true;
    return draw;
  }
  void SetRenderStats(const RenderStats& stats) { render_stats_ = stats; }

 private:
  std::shared_ptr<rendering::RenderTarget> target_;
  ViewportSettings settings_;
  std::shared_ptr<Scene> scene_ = Scene::Create();
  std::shared_ptr<Camera3D> camera_ = FPSCamera::Create();
  ui::Canvas canvas_;
  std::optional<glm::ivec2> fixed_size_;
  std::optional<float> screen_scale_;
  UpdateMode update_mode_ = UpdateMode::Always;
  glm::ivec2 last_size_{0, 0};
  bool has_picture_ = false;
  RenderStats render_stats_;
};

}  // namespace wlw::scene
