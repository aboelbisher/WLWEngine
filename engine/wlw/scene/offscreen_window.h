#pragma once

#include <algorithm>
#include <memory>
#include <string>

#include <glm/glm.hpp>

#include "wlw/platform/platform.h"
#include "wlw/rendering/render_target.h"
#include "wlw/scene/window.h"

namespace wlw::scene {

struct OffscreenWindowSettings {
  glm::ivec2 size = {1280, 720};       // the screen's size in pixels (a phone's: e.g. 1080 x 2340)
  platform::SafeAreaInsets safe_area;  // the simulated device's notch and system bars, in pixels
  bool touch = false;                  // its Input is a touch screen (a simulated phone)
  bool depth = true;                   // the screen gets a depth buffer (WindowOptions::depth)
};

// A window with no OS surface: a game's screen drawn into a texture, with an Input nobody but its owner
// feeds. The Studio runs a play session on one and shows GetTexture() in its Game panel at a device's size,
// like Unreal's Play In Editor viewport or Unity's Game view. To the game it is its window: GetSize(), the
// Input's SurfaceSize() and touch, GetSafeAreaInsets() and HasFocus() are the simulated device's.
//
// It draws with whatever GL context is current (the host's: Application::Internal().InitHosted), never
// presents and never closes by itself (RequestClose). Its input is fed through
// GetInput().Internal() between the frames of the app that runs on it, in pointer units = its pixels.
// Not GPU-timed (the renderer times its own OS window only). Game thread.
class OffscreenWindow final : public Window {
 public:
  static std::unique_ptr<OffscreenWindow> Create(const OffscreenWindowSettings& settings = {}) {
    return std::unique_ptr<OffscreenWindow>(new OffscreenWindow(settings));
  }

  // The game's screen: the same texture for the window's life (resizing keeps it). Like every GL texture, its
  // row 0 is the BOTTOM of the picture.
  const std::shared_ptr<rendering::WTexture2D>& GetTexture() const { return screen_target_->GetTexture(); }

  // The screen's size in pixels (a device preset, a rotation); the input's surface follows.
  void SetSize(glm::ivec2 pixels) {
    size_ = {static_cast<float>(std::max(pixels.x, 1)), static_cast<float>(std::max(pixels.y, 1))};
    input_.Internal().SetSurfaceSize(size_.x, size_.y);
  }
  void SetSafeAreaInsets(const platform::SafeAreaInsets& insets) { safe_area_ = insets; }
  void SetTouchScreen(bool touch) { input_.Internal().SetTouchScreen(touch); }
  // Whether it has the keyboard (the Studio: its Game panel is focused). Focused when made.
  void SetFocused(bool focused) { focused_ = focused; }

  // ---- Window ----
  bool Initialize() override { return true; }
  void MakeContextCurrent() override {}  // the host's context is current
  void ProcessEvents() override {}       // its owner feeds GetInput()
  void SwapBuffers() override {}
  bool ShouldClose() const override { return close_requested_; }
  void RequestClose() override { close_requested_ = true; }
  void SetTitle(const std::string&) override {}
  platform::SafeAreaInsets GetSafeAreaInsets() const override { return safe_area_; }

 private:
  explicit OffscreenWindow(const OffscreenWindowSettings& settings) : safe_area_(settings.safe_area) {
    screen_target_ = rendering::RenderTarget::Create({.depth = settings.depth});
    screen_depth_ = settings.depth;
    SetSize(settings.size);
    SetTouchScreen(settings.touch);
  }

  platform::SafeAreaInsets safe_area_;
  bool close_requested_ = false;
};

}  // namespace wlw::scene
