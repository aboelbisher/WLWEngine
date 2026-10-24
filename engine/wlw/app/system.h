#pragma once

#include <memory>
#include <string>
#include <vector>

namespace wlw::ui {
struct UIContext;
}

namespace wlw::app {

class Application;

// Something that runs for the app's whole life: the game's shared state and logic (its board,
// view, HUD), later engine features (physics, analytics...). Like Unreal's subsystems or Godot's
// autoloads; the other half is Screen, one state of the game at a time (menu, play, pause).
//
// Added with Application::AddSystem<T>(); each frame every system gets each hook, in the order
// they were added, before the screens get theirs (see Application for the whole frame). Every
// hook is optional. Systems live as long as the app: there is no removing them.
class System {
 public:
  virtual ~System() = default;

  virtual void OnStart() {}                      // the app is running: GPU ready, services up
  virtual void OnFixedUpdate(float dt) {}        // 0..N times a frame, constant dt: game logic
  virtual void OnUpdate(float dt) {}             // once a frame: input, camera (game time)
  virtual void OnRender(float alpha) {}          // state -> scene nodes, interpolated by alpha
  virtual void OnRenderUI(ui::UIContext& ui) {}  // 2D UI: draw with ui.canvas, widgets from ui/widgets.h
  virtual void OnDebugUI() {}                    // ImGui panels (WLW_DEBUG_UI builds, while open)
  // Android Back / desktop Escape that no screen handled; the last system added is asked first.
  // Return true if handled; if nobody does, the app goes to the background (desktop: quits).
  virtual bool OnBack() { return false; }
  // Went to the background: save now. Phones and browsers may end the app afterwards without an OnShutdown.
  virtual void OnAppPaused() {}
  virtual void OnAppResumed() {}  // came back from the background
  virtual void OnShutdown() {}    // the app is stopping (not on every platform: see OnAppPaused); services alive

  // Its type's name ("MyGame"; AddSystem sets it): the profiler names its hooks' zones with it.
  const std::string& Name() const { return name_; }

 protected:
  // The app's services (window, input, audio, screens, time...). Valid from OnStart on (not in
  // the constructor: the system isn't added yet).
  Application& App() const { return *app_; }

 private:
  friend class SystemList;
  Application* app_ = nullptr;
  std::string name_;
};

// The app's systems in order, and how the frame's phases reach them. Kept apart from Application
// so the dispatching can be tested without a window.
class SystemList {
 public:
  // Takes `system`, owned from now on. It's started (OnStart) by the next StartPending.
  System& Add(std::unique_ptr<System> system, Application* app, std::string name = "System") {
    system->app_ = app;
    system->name_ = std::move(name);
    systems_.push_back(std::move(system));
    return *systems_.back();
  }

  // OnStart for the systems added since the last call, in order (also those a starting system
  // adds). The Application calls it before the first frame and at the start of every frame.
  void StartPending() {
    while (started_ < systems_.size()) systems_[started_++]->OnStart();
  }

  // `hook(system)` for every started system, in the order added.
  template <typename Hook>
  void ForEach(Hook&& hook) {
    for (size_t i = 0; i < started_; ++i) hook(*systems_[i]);
  }

  // Back: the last added first, until one handles it.
  bool Back() {
    for (size_t i = started_; i-- > 0;) {
      if (systems_[i]->OnBack()) return true;
    }
    return false;
  }

  // The first system of type T (or derived), else null. A short search: for setup, not per frame.
  template <typename T>
  T* Get() const {
    for (const auto& system : systems_) {
      if (auto* found = dynamic_cast<T*>(system.get())) return found;
    }
    return nullptr;
  }

  size_t Size() const { return systems_.size(); }
  size_t Started() const { return started_; }

 private:
  std::vector<std::unique_ptr<System>> systems_;
  size_t started_ = 0;
};

}  // namespace wlw::app
