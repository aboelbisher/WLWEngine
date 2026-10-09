#include "wlw/debug/debug_ui.h"

#include <algorithm>
#include <cfloat>

#include "imgui.h"
#include "imgui_impl_opengl3.h"
#include "wlw/platform/platform.h"

namespace wlw::debug {

namespace {

#if defined(WLW_GLES)
constexpr const char* kImGuiGLSLVersion = "#version 300 es";
#else
constexpr const char* kImGuiGLSLVersion = "#version 330 core";
#endif

// UI scale. Desktop sizes are in window units (HiDPI is handled by the framebuffer scale), so 1.
// On a phone the surface is in physical pixels: ~400 px per short side ~ 1 "dp", a rough
// density estimate (a 1080 px wide phone gets ~2.7x) until the platform reports real density.
float UIScale(const input::Input& input) {
  if (!input.IsTouchScreen()) return 1.0f;
  const core::Vector2 size = input.SurfaceSize();
  return std::clamp(std::min(size.x, size.y) / 400.0f, 1.0f, 4.0f);
}

}  // namespace

DebugUI::DebugUI(scene::Window& window) : window_(window) {
  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImGuiIO& io = ImGui::GetIO();
  io.IniFilename = nullptr;  // don't write imgui.ini (window layout) next to the game
  io.BackendPlatformName = "wlw_input";
  if (window.GetInput().IsTouchScreen()) io.ConfigFlags |= ImGuiConfigFlags_IsTouchScreen;

  ImGui::StyleColorsDark();
  ImGuiStyle& style = ImGui::GetStyle();
  const float scale = UIScale(window.GetInput());
  style.ScaleAllSizes(scale);  // padding, spacing, scrollbars...
  style.FontScaleDpi = scale;  // ...and text (the vector font stays sharp at any size)
  io.Fonts->AddFontDefaultVector();

  ImGui_ImplOpenGL3_Init(kImGuiGLSLVersion);
  overlay_id_ = window_.AddOverlay([] {
    if (ImDrawData* draw_data = ImGui::GetDrawData()) ImGui_ImplOpenGL3_RenderDrawData(draw_data);
  });
}

DebugUI::~DebugUI() {
  window_.RemoveOverlay(overlay_id_);
  window_.GetInput().SetPointerBlocked(false);
  if (platform::HasCurrentGLContext()) ImGui_ImplOpenGL3_Shutdown();  // frees GL objects
  ImGui::DestroyContext();
}

void DebugUI::BeginFrame(float dt) {
  FeedInput(dt);
  ImGui_ImplOpenGL3_NewFrame();
  ImGui::NewFrame();

  if (window_.GetInput().WasKeyPressed(input::Key::F1)) open_ = !open_;
  if (hidden_for_ > 0.0f) {
    hidden_for_ -= dt;  // hidden (HideFor): no button, F1 still works
  } else {
    DrawToggleButton();
  }

  // Who owns the pointer this frame: if it's over (or dragging) an ImGui window, the game
  // doesn't see it. ImGui decides this in NewFrame from the current position and buttons.
  window_.GetInput().SetPointerBlocked(ImGui::GetIO().WantCaptureMouse);
}

void DebugUI::EndFrame() { ImGui::Render(); }

void DebugUI::FeedInput(float dt) {
  ImGuiIO& io = ImGui::GetIO();
  input::Input& input = window_.GetInput();
  input.SetPointerBlocked(false);  // decided again below, after ImGui has seen this frame

  io.DeltaTime = dt > 0.0f ? dt : 1.0f / 60.0f;
  // ImGui works in pointer units; the framebuffer may have more pixels (HiDPI desktops).
  const core::Vector2 surface = input.SurfaceSize();
  const core::Vector2 framebuffer = window_.GetSize();
  io.DisplaySize = ImVec2(surface.x, surface.y);
  if (surface.x > 0.0f && surface.y > 0.0f) {
    io.DisplayFramebufferScale = ImVec2(framebuffer.x / surface.x, framebuffer.y / surface.y);
  }

  const bool touch = input.IsTouchScreen();
  io.AddMouseSourceEvent(touch ? ImGuiMouseSource_TouchScreen : ImGuiMouseSource_Mouse);
  const core::Vector2 pointer = input.PointerPosition();
  if (!touch || input.RawPointerDown() || input.RawPointerPressed() || input.RawPointerReleased()) {
    io.AddMousePosEvent(pointer.x, pointer.y);
  } else {
    io.AddMousePosEvent(-FLT_MAX, -FLT_MAX);  // a lifted finger hovers nothing
  }

  // ImGui's input queue takes events in order, so a press and a release inside one frame (a
  // quick tap) still reach it as a click: it plays them back over consecutive frames.
  if (input.RawPointerPressed() && !imgui_pointer_down_) {
    io.AddMouseButtonEvent(ImGuiMouseButton_Left, true);
    imgui_pointer_down_ = true;
  }
  if (!input.RawPointerDown() && imgui_pointer_down_) {
    io.AddMouseButtonEvent(ImGuiMouseButton_Left, false);
    imgui_pointer_down_ = false;
  }
}

void DebugUI::DrawToggleButton() {
  const ImGuiIO& io = ImGui::GetIO();
  const float margin = 8.0f * ImGui::GetStyle().FontScaleDpi;
  ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x - margin, margin), ImGuiCond_Always, ImVec2(1.0f, 0.0f));
  ImGui::SetNextWindowBgAlpha(0.35f);
  constexpr ImGuiWindowFlags kFlags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize |
                                      ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing |
                                      ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoMove;
  ImGui::Begin("##wlw_debug_toggle", nullptr, kFlags);
  if (ImGui::Button(open_ ? "Close debug" : "Debug")) open_ = !open_;
  ImGui::End();
}

}  // namespace wlw::debug
