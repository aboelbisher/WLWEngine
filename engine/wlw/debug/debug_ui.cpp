#include "wlw/debug/debug_ui.h"

#include <algorithm>
#include <cfloat>
#include <optional>

#include "imgui.h"
#include "imgui_impl_opengl3.h"
#include "wlw/debug/imgui_input.h"
#include "wlw/platform/platform.h"

namespace wlw::debug {

namespace {

constexpr input::Key kModifierSides[] = {input::Key::LeftShift,    input::Key::RightShift, input::Key::LeftControl,
                                         input::Key::RightControl, input::Key::LeftAlt,    input::Key::RightAlt,
                                         input::Key::LeftSuper,    input::Key::RightSuper};

#if defined(WLW_GLES)
constexpr const char* kImGuiGLSLVersion = "#version 300 es";
#else
constexpr const char* kImGuiGLSLVersion = "#version 330 core";
#endif

// UI scale. Desktop: the OS's display scaling (ContentScale), in pointer units: where those are already larger
// than pixels (macOS points: the framebuffer scale draws them sharp) that part is taken out again. On a phone the
// surface is in physical pixels: ~400 px per short side ~ 1 "dp", a rough density estimate (a 1080 px wide phone
// gets ~2.7x) until the platform reports real density.
float UIScale(const scene::Window& window) {
  const input::Input& input = window.GetInput();
  const Vec2 size = input.SurfaceSize();
  if (input.IsTouchScreen()) return std::clamp(std::min(size.x, size.y) / 400.0f, 1.0f, 4.0f);
  const Vec2 framebuffer = window.GetSize();
  const float points = size.x > 0.0f && framebuffer.x > 0.0f ? framebuffer.x / size.x : 1.0f;
  return std::clamp(window.ContentScale() / points, 1.0f, 4.0f);
}

}  // namespace

DebugUI::DebugUI(scene::Window& window) : window_(window) {
  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImGuiIO& io = ImGui::GetIO();
  io.IniFilename = nullptr;  // don't write imgui.ini (window layout) next to the game
  io.BackendPlatformName = "wlw_input";
  io.BackendFlags |= ImGuiBackendFlags_HasMouseCursors;  // its cursor shapes go to the window (UpdateCursor)
  if (window.GetInput().IsTouchScreen()) io.ConfigFlags |= ImGuiConfigFlags_IsTouchScreen;

  ApplyScale();
  io.Fonts->AddFontDefaultVector();

  ImGui_ImplOpenGL3_Init(kImGuiGLSLVersion);
  overlay_ = window_.Internal().AddOverlay([] {
    if (ImDrawData* draw_data = ImGui::GetDrawData()) ImGui_ImplOpenGL3_RenderDrawData(draw_data);
  });
}

DebugUI::~DebugUI() {
  overlay_.Disconnect();
  window_.GetInput().SetPointerBlocked(false);
  window_.Internal().SetCursorOverride(std::nullopt);
  if (platform::HasCurrentGLContext()) ImGui_ImplOpenGL3_Shutdown();  // frees GL objects
  ImGui::DestroyContext();
}

void DebugUI::SetStudioMode(bool on) {
  studio_mode_ = on;
  if (on) open_ = true;
  ImGuiIO& io = ImGui::GetIO();
  if (on) {
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
  } else {
    io.ConfigFlags &= ~ImGuiConfigFlags_DockingEnable;
  }
}

void DebugUI::ApplyScale() {
  const float scale = UIScale(window_);
  if (scale == scale_) return;
  scale_ = scale;
  ImGuiStyle& style = ImGui::GetStyle();
  style = ImGuiStyle();  // ScaleAllSizes multiplies: start again from the unscaled sizes
  ImGui::StyleColorsDark();
  style.ScaleAllSizes(scale);  // padding, spacing, scrollbars...
  style.FontScaleDpi = scale;  // ...and text (the vector font stays sharp at any size)
}

void DebugUI::BeginFrame(float dt) {
  in_frame_ = true;
  FeedInput(dt);
  UpdateCursor();
  ApplyScale();  // moved to a monitor with another scale
  ImGui_ImplOpenGL3_NewFrame();
  ImGui::NewFrame();
  if (frame_start_) frame_start_();

  const input::Input& input = window_.GetInput();
  if (input.RawKeyPressed(input::Key::F1) && !studio_mode_) open_ = !open_;
  if (input.RawKeyPressed(input::Key::Grave)) {  // the console: opens the debug UI with it
    console_.open = !console_.open || !open_;
    if (console_.open) {
      open_ = true;
      console_.FocusInput();
    }
  }
  if (hidden_for_ > 0.0f) {
    hidden_for_ -= dt;         // hidden (HideFor): no button, F1 still works
  } else if (!studio_mode_) {  // the Studio: always open, no button
    DrawToggleButton();
  }

  if (open_ && console_.open) console_.Draw(!input.IsTouchScreen());

  // Who owns the pointer this frame: if it's over (or dragging) an ImGui window, the game
  // doesn't see it. ImGui decides this in NewFrame from the current position and buttons.
  window_.GetInput().SetPointerBlocked(ImGui::GetIO().WantCaptureMouse);
  // And the keyboard, while a text field (the console's input line) has it.
  window_.GetInput().SetKeyboardBlocked(ImGui::GetIO().WantTextInput);
}

void DebugUI::EndFrame() {
  ImGui::Render();
  in_frame_ = false;
}

void DebugUI::FeedInput(float dt) {
  ImGuiIO& io = ImGui::GetIO();
  input::Input& input = window_.GetInput();
  input.SetPointerBlocked(false);  // decided again below, after ImGui has seen this frame
  input.SetKeyboardBlocked(false);
  FeedKeyboard();

  io.DeltaTime = dt > 0.0f ? dt : 1.0f / 60.0f;
  // ImGui works in pointer units; the framebuffer may have more pixels (HiDPI desktops).
  const Vec2 surface = input.SurfaceSize();
  const Vec2 framebuffer = window_.GetSize();
  io.DisplaySize = ImVec2(surface.x, surface.y);
  if (surface.x > 0.0f && surface.y > 0.0f) {
    io.DisplayFramebufferScale = ImVec2(framebuffer.x / surface.x, framebuffer.y / surface.y);
  }

  if (window_.HasFocus() != imgui_focused_) {
    imgui_focused_ = window_.HasFocus();
    io.AddFocusEvent(imgui_focused_);
  }

  const bool touch = input.IsTouchScreen();
  io.AddMouseSourceEvent(touch ? ImGuiMouseSource_TouchScreen : ImGuiMouseSource_Mouse);
  const Vec2 pointer = input.PointerPosition();
  if (window_.IsCursorLocked()) {
    io.AddMousePosEvent(-FLT_MAX, -FLT_MAX);  // a locked cursor (mouse look) points at nothing
  } else if (!touch || input.RawPointerDown() || input.RawPointerPressed() || input.RawPointerReleased()) {
    io.AddMousePosEvent(pointer.x, pointer.y);
  } else {
    io.AddMousePosEvent(-FLT_MAX, -FLT_MAX);  // a lifted finger hovers nothing
  }

  // ImGui's input queue takes events in order, so a press and a release inside one frame (a
  // quick tap) still reach it as a click: it plays them back over consecutive frames. A finger is
  // the pointer; a mouse has its five buttons (ImGui's order is MouseButton's).
  for (int i = 0; i < ImGuiMouseButton_COUNT; ++i) {
    const auto button = static_cast<input::MouseButton>(i);
    const bool pressed = touch ? (i == 0 && input.RawPointerPressed()) : input.RawMouseButtonPressed(button);
    const bool down = touch ? (i == 0 && input.RawPointerDown()) : input.RawMouseButtonDown(button);
    if (pressed && !imgui_buttons_down_[i]) {
      io.AddMouseButtonEvent(i, true);
      imgui_buttons_down_[i] = true;
    }
    if (!down && imgui_buttons_down_[i]) {
      io.AddMouseButtonEvent(i, false);
      imgui_buttons_down_[i] = false;
    }
  }
  // ImGui's horizontal wheel is +x = left.
  if (const Vec2 scroll = input.RawScrollDelta(); scroll.x != 0.0f || scroll.y != 0.0f) {
    io.AddMouseWheelEvent(-scroll.x, scroll.y);
  }
}

// ImGui's cursor shape from the last frame goes to the window while ImGui has the mouse (resize borders, text
// fields); the game's shape otherwise.
void DebugUI::UpdateCursor() {
  const ImGuiIO& io = ImGui::GetIO();
  std::optional<scene::Cursor> cursor;
  if (io.WantCaptureMouse && (io.ConfigFlags & ImGuiConfigFlags_NoMouseCursorChange) == 0) {
    cursor = requested_cursor_    ? *requested_cursor_
             : io.MouseDrawCursor ? scene::Cursor::Hidden
                                  : FromImGuiCursor(ImGui::GetMouseCursor());
  }
  requested_cursor_.reset();  // asked for again every frame
  window_.Internal().SetCursorOverride(cursor);
}

void DebugUI::FeedKeyboard() {
  ImGuiIO& io = ImGui::GetIO();
  const input::Input& input = window_.GetInput();
  // Every key change in the OS's order (Ctrl before Z; a quick tap inside one frame: down, then up), by the
  // layout's key (D18). A modifier key also sets ImGui's modifier, held while either side is.
  for (const input::KeyEvent& event : input.RawKeyEvents()) {
    io.AddKeyEvent(ToImGuiKey(event.layout_key), event.down);
    if (const ImGuiKey modifier = ToImGuiModifier(event.key); modifier != ImGuiKey_None) {
      bool held = event.down;
      for (const input::Key side : kModifierSides) {
        if (side != event.key && ToImGuiModifier(side) == modifier && input.RawKeyDown(side)) held = true;
      }
      io.AddKeyEvent(modifier, held);
    }
  }
  const bool toggled_console = input.RawKeyPressed(input::Key::Grave);
  for (const uint32_t codepoint : input.RawText()) {
    if (toggled_console && (codepoint == '`' || codepoint == '~')) continue;  // the toggle isn't typed
    io.AddInputCharacter(codepoint);
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
  if (open_) {
    ImGui::SameLine();
    if (ImGui::Button(console_.open ? "Hide console" : "Console")) console_.open = !console_.open;
  }
  ImGui::End();
}

}  // namespace wlw::debug
