#include "wlw/studio/studio.h"

#include <string>
#include <vector>

#include "imgui.h"
#include "imgui_internal.h"  // DockBuilder, BeginViewportSideBar
#include "wlw/core/frame_pacing.h"
#include "wlw/debug/debug_ui.h"
#include "wlw/input/input.h"
#include "wlw/platform/platform.h"
#include "wlw/studio/studio_log.h"

namespace wlw::studio {

namespace {

using input::Key;
using input::Modifiers;

constexpr Modifiers kPlayKeys = Modifiers::Primary;                      // Play / Stop
constexpr Modifiers kPauseKeys = Modifiers::Primary | Modifiers::Shift;  // Pause / Resume
constexpr Modifiers kStepKeys = Modifiers::Primary | Modifiers::Alt;     // Step

}  // namespace

app::AppConfig StudioConfig(const char* game_name) {
  const std::string game = game_name != nullptr ? game_name : "";
  app::AppConfig config;
  config.name = game.empty() ? "wlw_studio" : game + "_studio";
  config.window.title = game.empty() ? "WLW Studio" : "WLW Studio - " + game;
  config.window.size = {1600, 900};
  config.window.stats_in_title = false;               // the status bar has them
  config.graphics.target_fps = core::kTargetDisplayRate;
  config.audio.enabled = false;                       // each play session has the game's own
  return config;
}

int RunStudio(int argc, char** argv, const char* game_name, MakeGameFn make_game) {
  // The Studio's command line reaches the game too (cvars: +r.hdr 1).
  platform::SetLaunchArguments(std::vector<std::string>(argv + (argc > 0 ? 1 : 0), argv + argc));
  auto studio = std::make_unique<app::Application>(StudioConfig(game_name));
  studio->AddSystem<StudioSystem>(game_name != nullptr ? game_name : "", make_game);
  return studio->Run();
}

void StudioSystem::OnStart() {
  session_ = std::make_unique<PlaySession>(App(), make_game_);
  game_panel_ = std::make_unique<GamePanel>(*session_);
  debug::DebugUI& ui = App().DebugUi();
  ui.SetStudioMode(true);
  ui.Console().open = true;
  // The layout is the Studio's, kept in its save folder (read at the first frame).
  if (const std::string folder = platform::GetSaveDirectory(App().Config().name); !folder.empty()) {
    layout_file_ = folder + "/studio_layout.ini";
    ImGui::GetIO().IniFilename = layout_file_.c_str();
  }
  ui.SetFrameStart([this] { DrawFrameStart(); });
  WLW_LOG(LogStudio, Info, "Studio: %s", make_game_ != nullptr ? game_name_.c_str() : "no game (engine types only)");
}

void StudioSystem::OnShutdown() {
  session_->Stop();
  debug::DebugUI& ui = App().DebugUi();
  ui.SetFrameStart(nullptr);
  if (!layout_file_.empty()) {  // now: ImGui would save it after this system (and the file name) is gone
    ImGui::SaveIniSettingsToDisk(layout_file_.c_str());
    ImGui::GetIO().IniFilename = nullptr;
  }
}

void StudioSystem::OnDebugUI() {
  ApplyPending();
  game_panel_->BeforeGameFrame(App());
  session_->Frame();  // the game's frame: its screen into the Game panel's texture, its panels into this frame
  game_panel_->Draw(App());
  DockNewWindows();
}

void StudioSystem::DockNewWindows() {
  const ImGuiWindow* engine = ImGui::FindWindowByName("Engine");
  if (engine == nullptr || engine->DockId == 0) return;
  constexpr ImGuiWindowFlags kNotPanels = ImGuiWindowFlags_ChildWindow | ImGuiWindowFlags_Popup |
                                          ImGuiWindowFlags_Tooltip | ImGuiWindowFlags_NoDocking |
                                          ImGuiWindowFlags_NoSavedSettings;
  for (const ImGuiWindow* window : ImGui::GetCurrentContext()->Windows) {
    // New: not docked, no saved place (SettingsOffset: the layout file has never had it).
    if ((window->Flags & kNotPanels) != 0 || window->IsFallbackWindow || !window->Active || window->DockId != 0 ||
        window->SettingsOffset != -1) {
      continue;
    }
    if (docked_.insert(window->Name).second) ImGui::DockBuilderDockWindow(window->Name, engine->DockId);
  }
}

void StudioSystem::ApplyPending() {
  const Pending pending = pending_;
  pending_ = Pending::None;
  switch (pending) {
    case Pending::None:
      break;
    case Pending::Play:
      session_->Play(game_panel_->Screen());
      break;
    case Pending::Stop:
      session_->Stop();
      break;
    case Pending::TogglePause:
      session_->SetPaused(!session_->Paused());
      break;
    case Pending::Step:
      session_->SetPaused(true);
      session_->Step();
      break;
  }
}

void StudioSystem::HandleShortcuts() {
  const input::Input& input = App().Input();
  if (input.WasShortcutPressed(Key::P, kPlayKeys) && session_->CanPlay()) {
    session_->Playing() ? Stop() : Play();
  }
  if (input.WasShortcutPressed(Key::P, kPauseKeys) && session_->Playing()) TogglePause();
  if (input.WasShortcutPressed(Key::P, kStepKeys) && session_->Playing()) Step();
  if (input.WasShortcutPressed(Key::Q, Modifiers::Primary)) App().Quit();
}

void StudioSystem::DrawFrameStart() {
  HandleShortcuts();
  DrawMenuBar();
  ImGuiViewport* viewport = ImGui::GetMainViewport();
  constexpr ImGuiWindowFlags kBarFlags =
      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_MenuBar;
  if (ImGui::BeginViewportSideBar("##studio_toolbar", viewport, ImGuiDir_Up, ImGui::GetFrameHeight(), kBarFlags)) {
    if (ImGui::BeginMenuBar()) {
      DrawToolbar();
      ImGui::EndMenuBar();
    }
  }
  ImGui::End();
  if (ImGui::BeginViewportSideBar("##studio_status", viewport, ImGuiDir_Down, ImGui::GetFrameHeight(), kBarFlags)) {
    if (ImGui::BeginMenuBar()) {
      DrawStatusBar();
      ImGui::EndMenuBar();
    }
  }
  ImGui::End();

  // The dockspace over the rest of the window; the default layout when there is none yet (first run, no layout
  // file) or on Window > Reset layout.
  const ImGuiID dockspace = ImGui::GetID("##studio_dockspace");
  if (reset_layout_ || ImGui::DockBuilderGetNode(dockspace) == nullptr) {
    reset_layout_ = false;
    BuildDefaultLayout(dockspace);
  }
  ImGui::DockSpaceOverViewport(dockspace, viewport);
}

void StudioSystem::BuildDefaultLayout(unsigned int dockspace) {
  const ImGuiViewport* viewport = ImGui::GetMainViewport();
  ImGui::DockBuilderRemoveNode(dockspace);
  ImGui::DockBuilderAddNode(dockspace, ImGuiDockNodeFlags_DockSpace);
  ImGui::DockBuilderSetNodeSize(dockspace, viewport->WorkSize);
  ImGuiID center = dockspace;
  const ImGuiID right = ImGui::DockBuilderSplitNode(center, ImGuiDir_Right, 0.28f, nullptr, &center);
  const ImGuiID bottom = ImGui::DockBuilderSplitNode(center, ImGuiDir_Down, 0.28f, nullptr, &center);
  ImGui::DockBuilderDockWindow("Game", center);
  ImGui::DockBuilderDockWindow("Engine", right);
  ImGui::DockBuilderDockWindow("CVars", right);
  ImGui::DockBuilderDockWindow("Console", bottom);
  ImGui::DockBuilderFinish(dockspace);
}

void StudioSystem::DrawMenuBar() {
  if (!ImGui::BeginMainMenuBar()) return;
  if (ImGui::BeginMenu("File")) {
    if (ImGui::MenuItem("Quit", ShortcutName(Key::Q, Modifiers::Primary).c_str())) App().Quit();
    ImGui::EndMenu();
  }
  if (ImGui::BeginMenu("Play")) {
    const bool playing = session_->Playing();
    if (ImGui::MenuItem(playing ? "Stop" : "Play", ShortcutName(Key::P, kPlayKeys).c_str(), false,
                        session_->CanPlay())) {
      playing ? Stop() : Play();
    }
    if (ImGui::MenuItem("Pause", ShortcutName(Key::P, kPauseKeys).c_str(), session_->Paused(), playing)) {
      TogglePause();
    }
    if (ImGui::MenuItem("Step", ShortcutName(Key::P, kStepKeys).c_str(), false, playing)) Step();
    ImGui::EndMenu();
  }
  if (ImGui::BeginMenu("Window")) {
    ImGui::MenuItem("Console", ShortcutName(Key::Grave, Modifiers::None).c_str(), &App().DebugUi().Console().open);
    ImGui::Separator();
    if (ImGui::MenuItem("Reset layout")) reset_layout_ = true;
    ImGui::EndMenu();
  }
  ImGui::EndMainMenuBar();
}

void StudioSystem::DrawToolbar() {
  const bool playing = session_->Playing();
  ImGui::BeginDisabled(!session_->CanPlay());
  if (playing) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.20f, 0.55f, 0.30f, 1.0f));
  if (ImGui::Button(playing ? "Stop" : "Play")) playing ? Stop() : Play();
  if (playing) ImGui::PopStyleColor();
  ImGui::SetItemTooltip("%s", ShortcutName(Key::P, kPlayKeys).c_str());
  ImGui::BeginDisabled(!playing);
  if (ImGui::Button(session_->Paused() ? "Resume" : "Pause")) TogglePause();
  ImGui::SetItemTooltip("%s", ShortcutName(Key::P, kPauseKeys).c_str());
  if (ImGui::Button("Step")) Step();
  ImGui::SetItemTooltip("One fixed step (%s)", ShortcutName(Key::P, kStepKeys).c_str());
  ImGui::EndDisabled();
  ImGui::EndDisabled();
  ImGui::Separator();
  game_panel_->DrawDeviceControls();
}

void StudioSystem::DrawStatusBar() {
  ImGui::TextDisabled("Studio %.0f fps", App().Time().Fps());
  ImGui::Separator();
  if (app::Application* game = session_->Game()) {
    ImGui::Text("%s: %.0f fps, %.2f ms", session_->Paused() ? "Paused" : "Playing", game->Time().Fps(),
                game->Time().FrameMs());
  } else if (const std::optional<int> code = session_->LastExitCode()) {
    ImGui::TextDisabled("Stopped: the game quit (exit code %d)", *code);
  } else {
    ImGui::TextDisabled("%s", session_->CanPlay() ? "Stopped" : "No game in this Studio");
  }
}

}  // namespace wlw::studio
