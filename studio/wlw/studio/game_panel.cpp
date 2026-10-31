#include "wlw/studio/game_panel.h"

#include <algorithm>
#include <cstdio>
#include <string>

#include "imgui.h"
#include "wlw/debug/debug_ui.h"
#include "wlw/debug/imgui_texture.h"

namespace wlw::studio {

namespace {

constexpr float kMaxZoom = 8.0f;  // a small device in a large panel: drawn larger, up to this

ImVec2 ToImVec(Vec2 v) { return {v.x, v.y}; }

}  // namespace

DeviceScreen GamePanel::Screen() const {
  const std::span<const DevicePreset> presets = DevicePresets();
  return ScreenFor(presets[std::clamp(device_, 0, static_cast<int>(presets.size()) - 1)], turned_, free_size_);
}

void GamePanel::SetStudioCursorLocked(app::Application& studio, bool locked) {
  if (locked == studio_cursor_locked_) return;
  studio_cursor_locked_ = locked;
  studio.Window().SetCursorLocked(locked);
}

void GamePanel::BeforeGameFrame(app::Application& studio) {
  scene::OffscreenWindow* window = session_.Window();
  if (window == nullptr) {
    SetStudioCursorLocked(studio, false);
    forwarded_game_ = nullptr;
    return;
  }
  if (session_.Game() != forwarded_game_) {  // a new session: a fresh Input
    forwarded_game_ = session_.Game();
    forwarder_.Reset();
    lock_released_ = false;
  }
  const DeviceScreen screen = Screen();
  session_.SetScreen(screen);
  window->SetFocused(focused_);

  // The game's cursor lock holds the Studio's cursor while the panel has focus, until Shift+F1.
  const input::Input& input = studio.Input();
  if (input.RawKeyPressed(input::Key::F1) && input.RawModifiers() == input::Modifiers::Shift) lock_released_ = true;
  if (hovered_ && input.RawPointerPressed()) lock_released_ = false;
  const bool locked = window->IsCursorLocked() && focused_ && !lock_released_;
  SetStudioCursorLocked(studio, locked);

  GameInputFrame frame;
  frame.rect = rect_;
  frame.device = screen.size;
  frame.hovered = hovered_ && rect_.Contains(input.PointerPosition());
  frame.focused = focused_;
  frame.touch = screen.touch;
  frame.cursor_locked = locked;
  forwarder_.Forward(input, window->GetInput(), frame);
}

void GamePanel::Draw(app::Application& studio) {
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
  const bool visible =
      ImGui::Begin("Game", nullptr, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
  ImGui::PopStyleVar();
  hovered_ = false;
  focused_ = visible && ImGui::IsWindowFocused();
  if (visible) {
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const ImVec2 area = ImGui::GetContentRegionAvail();
    const ImVec2 pixels_per_unit = ImGui::GetIO().DisplayFramebufferScale;
    free_size_ = glm::max(glm::ivec2(static_cast<int>(area.x * pixels_per_unit.x),
                                     static_cast<int>(area.y * pixels_per_unit.y)),
                          glm::ivec2(1));
    ImGui::GetWindowDrawList()->AddRectFilled(origin, ImVec2(origin.x + area.x, origin.y + area.y),
                                              IM_COL32(24, 24, 28, 255));
    if (scene::OffscreenWindow* window = session_.Window()) {
      const Vec2 size = window->GetSize();
      rect_ = FitGameRect({origin.x, origin.y}, {area.x, area.y}, glm::ivec2(size), kMaxZoom);
      if (rect_.size.x >= 1.0f && rect_.size.y >= 1.0f) {
        // An invisible button over the picture: clicks and drags there are the game's, not the panel's (a floating
        // panel would move).
        ImGui::SetCursorScreenPos(ToImVec(rect_.min));
        ImGui::InvisibleButton("##game", ToImVec(rect_.size),
                               ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight |
                                   ImGuiButtonFlags_MouseButtonMiddle);
        hovered_ = ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);
        ImGui::GetWindowDrawList()->AddImage(debug::ImGuiTexture(*window->GetTexture()), ToImVec(rect_.min),
                                             ToImVec(rect_.min + rect_.size), debug::kImGuiFlippedUv0,
                                             debug::kImGuiFlippedUv1);
        if (show_safe_area_) DrawSafeArea();
        if (hovered_) studio.DebugUi().RequestCursor(window->GetCursor());  // the game's shape over the game
      }
    } else {
      rect_ = {};
      char text[160];
      if (!session_.CanPlay()) {
        std::snprintf(text, sizeof(text), "No game in this Studio: build your game's <game>_studio to play it");
      } else if (const std::optional<int> code = session_.LastExitCode()) {
        std::snprintf(text, sizeof(text), "The game quit (exit code %d). Press Play to start it again.", *code);
      } else {
        std::snprintf(text, sizeof(text), "Press Play");
      }
      const ImVec2 text_size = ImGui::CalcTextSize(text);
      ImGui::SetCursorScreenPos(ImVec2(origin.x + std::max(0.0f, (area.x - text_size.x) * 0.5f),
                                       origin.y + std::max(0.0f, (area.y - text_size.y) * 0.5f)));
      ImGui::TextDisabled("%s", text);
    }
  }
  ImGui::End();
}

void GamePanel::DrawSafeArea() const {
  const scene::OffscreenWindow* window = session_.Window();
  const Vec2 device = window->GetSize();
  const platform::SafeAreaInsets insets = window->GetSafeAreaInsets();
  const Vec2 scale = rect_.size / device;
  const Vec2 lo = rect_.min + Vec2(insets.left, insets.top) * scale;
  const Vec2 hi = rect_.min + rect_.size - Vec2(insets.right, insets.bottom) * scale;
  ImDrawList* draw = ImGui::GetWindowDrawList();
  const ImU32 shade = IM_COL32(255, 80, 80, 60);
  const Vec2 end = rect_.min + rect_.size;
  draw->AddRectFilled(ToImVec(rect_.min), ImVec2(end.x, lo.y), shade);  // the covered edges
  draw->AddRectFilled(ImVec2(rect_.min.x, hi.y), ToImVec(end), shade);
  draw->AddRectFilled(ImVec2(rect_.min.x, lo.y), ImVec2(lo.x, hi.y), shade);
  draw->AddRectFilled(ImVec2(hi.x, lo.y), ImVec2(end.x, hi.y), shade);
  draw->AddRect(ToImVec(lo), ToImVec(hi), IM_COL32(255, 80, 80, 200));  // where UI is safe
}

void GamePanel::DrawDeviceControls() {
  const std::span<const DevicePreset> presets = DevicePresets();
  const DeviceScreen screen = Screen();
  ImGui::SetNextItemWidth(ImGui::GetFontSize() * 11.0f);
  if (ImGui::BeginCombo("##device", std::string(presets[device_].name).c_str())) {
    for (int i = 0; i < static_cast<int>(presets.size()); ++i) {
      if (ImGui::Selectable(std::string(presets[i].name).c_str(), i == device_)) device_ = i;
    }
    ImGui::EndCombo();
  }
  ImGui::SameLine();
  ImGui::Checkbox("Turn", &turned_);
  ImGui::SetItemTooltip("A quarter turn: landscape on a phone");
  ImGui::SameLine();
  ImGui::Checkbox("Safe area", &show_safe_area_);
  ImGui::SameLine();
  ImGui::TextDisabled("%d x %d%s", screen.size.x, screen.size.y, screen.touch ? ", touch" : "");
}

}  // namespace wlw::studio
