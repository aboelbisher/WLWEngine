#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <glm/glm.hpp>

#include "wlw/core/color.h"
#include "wlw/rendering/post_pass.h"
#include "wlw/rendering/render_target.h"
#include "wlw/scene/camera_3d.h"
#include "wlw/scene/scene.h"
#include "wlw/ui/canvas.h"

namespace wlw::scene {

// What the renderer did for a viewport's last drawn frame (shown in the debug UI).
struct RenderStats {
  int draw_calls = 0;              // one per mesh drawn; an instanced node is one call for all its copies
  int transparent_draw_calls = 0;  // of those, drawn in the transparent queue (blending, back to front)
  int instances = 0;               // copies drawn by instanced calls (sprite batches' dots too)
  int instance_bytes = 0;          // per-copy data uploaded to the GPU this frame (only what changed)
  int culled_nodes = 0;            // skipped by frustum culling
  // The size its 3D was drawn at (RenderSize: smaller than the viewport below render scale 1).
  int render_width = 0;
  int render_height = 0;
  // GPU state work (main pass + shadow pass): what changed, and what the state cache saved.
  int program_changes = 0;
  int texture_binds = 0;
  int state_calls_skipped = 0;
  // CPU time spent on it, in microseconds.
  float gather_us = 0.0f;  // walking the scene, culling, building the draw list
  float sort_us = 0.0f;    // sorting the draw list
  float submit_us = 0.0f;  // shadow + main pass: GL calls
  // Post passes (the 3D went into the scene picture first; see post_pass.h). A scaled viewport
  // without passes has one: the copy that stretches its picture (kUpscalePass).
  int post_passes = 0;
  int post_targets = 0;  // textures the passes before the last write into
  int bloom_levels = 0;  // of those steps, bloom levels (post_settings.h; 0 = no bloom)
  // Draws into the distortion buffer (DistortionSettings; counted in draw_calls too) and its size (0 = not drawn).
  int distortion_draws = 0;
  int distortion_width = 0;
  int distortion_height = 0;
  float post_us = 0.0f;   // their GL calls
  int invalidations = 0;  // glInvalidateFramebuffer calls (phones: pictures not loaded / stored)
  // The format its 3D was drawn in: EffectiveFormat(), or RGBA8 where the GPU fell back.
  rendering::TextureFormat picture_format = rendering::TextureFormat::RGBA8;
};

// The GPU's time for a viewport (Window::SetGpuTiming), smoothed, in ms; -1 = not measured
// (timing off, unsupported, or not drawn yet). Arrives a few frames late (see gpu_timer.h).
struct GpuStats {
  float scene_ms = -1.0f;      // 3D: shadow + main pass
  float post_ms = -1.0f;       // all its post passes
  float canvas_ms = -1.0f;     // its 2D canvas
  std::vector<float> pass_ms;  // each post pass (scaled without passes: [0] = the upscale copy)
};

// Where a canvas layer's 2D goes (like a Unity Canvas's render mode).
enum class CanvasMode {
  Overlay,  // after the post passes: never blurred or bloomed (HUD, buttons, text)
  Picture,  // part of the viewport's picture: after its 3D, before its post passes
};

struct CanvasLayerSettings {
  CanvasMode mode = CanvasMode::Overlay;
  int order = 0;  // lower draws first, within its mode; equal orders draw in the order added
};

struct ViewportSettings {
  std::string name = "Viewport";                                      // shown in the debug UI
  rendering::TextureFormat format = rendering::TextureFormat::RGBA8;  // RGBA16F: glow, values above 1 (SetFormat)
  bool depth = true;   // a depth buffer; false for a 2D-only viewport (saves memory)
  bool smooth = true;  // linear filtering when its texture is sampled
  // Each drawn frame starts from this colour; nullopt keeps the last picture (trails, painting).
  std::optional<Color> clear = Color{0.0f, 0.0f, 0.0f, 0.0f};
};

// "Draw this scene, from this camera, into this picture" - like Godot's SubViewport. It shows a
// Scene (which it only points at: several viewports may share one), through a camera, with a 2D
// Canvas drawn over the 3D. The window's own viewport is the screen (Window::MainViewport()); the
// others draw into a texture before the screen, in the order they were added
// (Window::AddViewport), so a viewport may use the texture of one added before it.
//
//   std::shared_ptr<scene::Viewport> view = scene::Viewport::Create({.format = TextureFormat::RGBA16F, .depth =
//   false}); view->SetSizeFromScreen(0.5f);          // half resolution, follows rotation / resizes
//   view->SetCamera(top_down_camera);
//   view->GetScene()->Root().AddChild(particles);   // its own scene; or SetScene(shared_world)
//   window.AddViewport(view);
//   material->SetParam("uPicture", view->GetTexture());
class Viewport {
 public:
  enum class UpdateMode {
    Always,    // drawn every frame
    Once,      // drawn on the next frame, then kept (static content: costs nothing afterwards)
    Disabled,  // not drawn; the texture keeps its last picture (redrawn once if a resize lost it)
  };

  // An offscreen viewport with its own (empty) scene, drawing into a texture of its own.
  static std::unique_ptr<Viewport> Create(const ViewportSettings& settings = {}) {
    rendering::RenderTargetSettings target{settings.format, settings.smooth, settings.depth};
    return std::unique_ptr<Viewport>(new Viewport(rendering::RenderTarget::Create(target), settings));
  }

  // ---- What it shows ----
  void SetScene(std::shared_ptr<Scene> scene) { scene_ = std::move(scene); }  // null: no 3D, only the canvas
  const std::shared_ptr<Scene>& GetScene() const { return scene_; }
  void SetCamera(std::shared_ptr<Camera3D> camera) { camera_ = std::move(camera); }
  const std::shared_ptr<Camera3D>& GetCamera() const { return camera_; }
  // Its default 2D layer (Overlay, order 0: drawn after the post passes, never post-processed):
  // emptied and sized to the viewport every frame (in the app's UI units, so a
  // screen-proportioned viewport has the same Size() as the screen's canvas); draw into it during
  // OnRenderUI. The screen's is the game UI's (UIContext, widgets).
  ui::Canvas& GetCanvas() { return canvas_; }
  const ui::Canvas& GetCanvas() const { return canvas_; }

  // ---- Canvas layers (like Unity's Canvases with a render mode and sort order) ----
  // Another 2D layer, filled each frame like GetCanvas(). Picture layers are part of the
  // viewport's picture (drawn after its 3D, before its post passes: blurred, bloomed...); Overlay
  // layers go on top after the passes. The frame:
  //   3D -> Picture layers (by order) -> post passes -> Overlay layers (by order, GetCanvas() at 0)
  // The reference stays valid until the layer is removed.
  ui::Canvas& AddCanvasLayer(const CanvasLayerSettings& settings = {}) {
    CanvasLayer layer{settings, std::make_unique<ui::Canvas>()};
    // After every layer that draws before or with it: equal orders keep the order added.
    const auto at = std::upper_bound(layers_.begin(), layers_.end(), layer,
                                     [](const CanvasLayer& a, const CanvasLayer& b) { return DrawsBefore(a, b); });
    return *layers_.insert(at, std::move(layer))->owned;
  }
  // Removes a layer added with AddCanvasLayer (not the default one).
  void RemoveCanvasLayer(const ui::Canvas& layer) {
    std::erase_if(layers_, [&layer](const CanvasLayer& entry) { return entry.owned.get() == &layer; });
  }
  int CanvasLayerCount() const { return static_cast<int>(layers_.size()); }  // the default one included

  // ---- Its picture ----
  // The texture it draws into (null for the screen). The same object for the viewport's life.
  const std::shared_ptr<rendering::WTexture2D>& GetTexture() const {
    static const std::shared_ptr<rendering::WTexture2D> kNone;
    return target_ ? target_->GetTexture() : kNone;
  }
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

  // ---- Render scale (like Unity URP's renderScale, Godot's scaling_3d_scale) ----
  // Its 3D (and Picture canvas layers) drawn at a fraction of its size, then stretched to fill it
  // with a bilinear filter (nearest if the viewport isn't `smooth`); Overlay layers (the game UI)
  // are drawn after that at full size, so they stay sharp. Below 1 the GPU colours fewer pixels
  // (0.75 = 56% of them; phones are mostly limited by that) and the 3D gets softer; above 1 =
  // supersampling (screenshots). Post passes keep their sizes (fractions of the viewport's, so an
  // effect looks the same at every scale): the first reads the smaller picture, and the last
  // writes the full-size one anyway, so with passes the stretch costs nothing extra (without, one
  // copy). The screen's: the game's, QualitySettings::render_scales, or the r.render_scale cvar.
  static constexpr float kMinRenderScale = 0.25f;
  static constexpr float kMaxRenderScale = 2.0f;
  void SetRenderScale(float scale) { render_scale_ = ClampRenderScale(scale); }
  float GetRenderScale() const { return render_scale_; }
  // A scale that wins over SetRenderScale's while it is set (the r.render_scale cvar sets the
  // screen's); 0 = none.
  void SetRenderScaleOverride(float scale) { render_scale_override_ = scale > 0.0f ? ClampRenderScale(scale) : 0.0f; }
  float GetRenderScaleOverride() const { return render_scale_override_; }
  // The scale it is drawn at: the override if one is set, else its own.
  float EffectiveRenderScale() const { return render_scale_override_ > 0.0f ? render_scale_override_ : render_scale_; }
  // The size its 3D is drawn at when the viewport is `pixels` big (PixelSize): `pixels` x the
  // scale, rounded, at least 1 x 1 (`pixels` itself at scale 1, or while it has no size).
  glm::ivec2 RenderSize(glm::ivec2 pixels) const {
    const float scale = EffectiveRenderScale();
    if (scale == 1.0f || pixels.x <= 0 || pixels.y <= 0) return pixels;
    return {std::max(1, static_cast<int>(std::lround(static_cast<float>(pixels.x) * scale))),
            std::max(1, static_cast<int>(std::lround(static_cast<float>(pixels.y) * scale)))};
  }

  // ---- Picture format (HDR) ----
  // What its 3D and Picture layers are drawn into: RGBA16F keeps values above 1 (glow, bright
  // highlights) for its post passes and the output pass's tone mapping (post_settings.h). The
  // screen itself stays 8-bit: an HDR screen draws through its scene picture, and the output pass
  // (or a copy) brings it down. An offscreen viewport's texture changes format too. A GPU that
  // can't draw into RGBA16F falls back to RGBA8 (with a warning; the engine panel shows it).
  // Like Unity's camera HDR switch or Godot's use_hdr_2d.
  void SetFormat(rendering::TextureFormat format) {
    settings_.format = format;
    ApplyTargetFormat();
  }
  // A format that wins over SetFormat's while set (the r.hdr cvar sets the screen's); nullopt = none.
  void SetFormatOverride(std::optional<rendering::TextureFormat> format) {
    format_override_ = format;
    ApplyTargetFormat();
  }
  const std::optional<rendering::TextureFormat>& GetFormatOverride() const { return format_override_; }
  // The format it is drawn in: the override if set, else its own.
  rendering::TextureFormat EffectiveFormat() const { return format_override_.value_or(settings_.format); }

  void SetUpdateMode(UpdateMode mode) { update_mode_ = mode; }
  UpdateMode GetUpdateMode() const { return update_mode_; }
  void SetClearColor(std::optional<Color> color) { settings_.clear = color; }
  const std::optional<Color>& GetClearColor() const { return settings_.clear; }
  const ViewportSettings& Settings() const { return settings_; }

  // ---- Post passes (rendering/post_pass.h; ready-made: scene/post_effects.h) ----
  // Full-screen shaders on its 3D picture, in the order added; the last one writes the viewport's
  // picture (the screen or its texture). Its canvas is drawn after them, unaffected.
  void AddPostPass(rendering::PostPass pass) { post_.Add(std::move(pass)); }
  void ClearPostPasses() { post_.Clear(); }
  rendering::PostChain& Post() { return post_; }
  const rendering::PostChain& Post() const { return post_; }

  const RenderStats& GetRenderStats() const { return render_stats_; }
  const GpuStats& GetGpuStats() const { return gpu_stats_; }

  class InternalAccess;
  // Engine-only parts (the renderer's and the Application's): see InternalAccess below.
  InternalAccess Internal();

 private:
  friend class Window;  // makes the screen's (CreateScreen)

  // `target` null = draws to the window (the screen).
  Viewport(std::shared_ptr<rendering::RenderTarget> target, const ViewportSettings& settings)
      : target_(std::move(target)), settings_(settings) {}
  static std::unique_ptr<Viewport> CreateScreen() {
    return std::unique_ptr<Viewport>(
        new Viewport(nullptr, ViewportSettings{.name = "Screen", .clear = Color{0.1f, 0.1f, 0.15f, 1.0f}}));
  }

  std::shared_ptr<rendering::RenderTarget> target_;
  ViewportSettings settings_;
  std::shared_ptr<Scene> scene_ = Scene::Create();
  std::shared_ptr<Camera3D> camera_ = Camera3D::Create();
  ui::Canvas canvas_;
  // Every 2D layer in drawing order: Picture ones, then Overlay ones, each by order. The default
  // one (canvas_) has no `owned` canvas.
  struct CanvasLayer {
    CanvasLayerSettings settings;
    std::unique_ptr<ui::Canvas> owned;
  };
  static bool DrawsBefore(const CanvasLayer& a, const CanvasLayer& b) {
    const bool a_picture = a.settings.mode == CanvasMode::Picture;
    const bool b_picture = b.settings.mode == CanvasMode::Picture;
    if (a_picture != b_picture) return a_picture;
    return a.settings.order < b.settings.order;
  }
  std::vector<CanvasLayer> layers_ = DefaultLayers();
  static std::vector<CanvasLayer> DefaultLayers() {
    std::vector<CanvasLayer> layers;
    layers.push_back({{CanvasMode::Overlay, 0}, nullptr});
    return layers;
  }
  std::optional<glm::ivec2> fixed_size_;
  std::optional<float> screen_scale_;
  float render_scale_ = 1.0f;
  float render_scale_override_ = 0.0f;  // 0 = none
  std::optional<rendering::TextureFormat> format_override_;
  // An offscreen viewport's texture is its picture: it follows the format (reallocated before the next draw).
  void ApplyTargetFormat() {
    if (target_) target_->Internal().SetFormat(EffectiveFormat());
  }
  static float ClampRenderScale(float scale) {
    return std::isfinite(scale) ? std::clamp(scale, kMinRenderScale, kMaxRenderScale) : 1.0f;
  }
  UpdateMode update_mode_ = UpdateMode::Always;
  glm::ivec2 last_size_{0, 0};
  bool has_picture_ = false;
  rendering::PostChain post_;
  RenderStats render_stats_;
  GpuStats gpu_stats_;
  uint32_t id_ = NextId();

  static uint32_t NextId() {
    static uint32_t next = 1;
    return next++;
  }
};

// For the renderer and the Application, not games.
class Viewport::InternalAccess {
 public:
  explicit InternalAccess(Viewport& viewport) : viewport_(viewport) {}

  // Unique for the program's run (GPU timing results find their viewport by it, frames later).
  uint32_t Id() const { return viewport_.id_; }
  rendering::RenderTarget* GetTarget() const { return viewport_.target_.get(); }
  GpuStats& MutableGpuStats() const { return viewport_.gpu_stats_; }
  void SetRenderStats(const RenderStats& stats) const { viewport_.render_stats_ = stats; }

  // Whether to draw it this frame, at `pixel_size`: applies the update mode (Once becomes
  // Disabled), and redraws a kept picture once after a resize threw it away.
  bool BeginDraw(glm::ivec2 pixel_size) const {
    Viewport& v = viewport_;
    if (pixel_size.x <= 0 || pixel_size.y <= 0) return false;
    const bool resized = pixel_size != v.last_size_;
    v.last_size_ = pixel_size;
    const bool draw = v.update_mode_ != UpdateMode::Disabled || (resized && v.has_picture_);
    if (v.update_mode_ == UpdateMode::Once) v.update_mode_ = UpdateMode::Disabled;
    if (draw) v.has_picture_ = true;
    return draw;
  }

  // Calls visit(canvas) for each layer of `mode`, in drawing order.
  template <typename Visit>
  void ForEachCanvasLayer(CanvasMode mode, Visit&& visit) const {
    for (CanvasLayer& layer : viewport_.layers_) {
      if (layer.settings.mode == mode) visit(layer.owned ? *layer.owned : viewport_.canvas_);
    }
  }
  // Empties every layer and sizes it for `pixels` (the Application does it each frame).
  void BeginCanvases(glm::vec2 pixels, glm::vec2 reference) const {
    for (CanvasLayer& layer : viewport_.layers_) {
      (layer.owned ? *layer.owned : viewport_.canvas_).Internal().BeginFrame(pixels, reference);
    }
  }

 private:
  Viewport& viewport_;
};

inline Viewport::InternalAccess Viewport::Internal() { return InternalAccess(*this); }

}  // namespace wlw::scene
