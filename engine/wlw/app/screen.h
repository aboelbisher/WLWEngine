#pragma once

#include <memory>
#include <vector>

namespace wlw::ui {
struct UIContext;
}

namespace wlw::app {

class ScreenStack;

// One state of the game - a menu, the game itself, a pause or game-over panel - with its own
// input, update and drawing. Screens live on a ScreenStack; only the top one is "active".
//
// Lifecycle:  OnEnter()       pushed (or swapped in)
//             OnCovered()     another screen was pushed on top: stop reacting, you're frozen
//             OnUncovered()   the screen above was popped: carry on
//             OnExit()        popped / replaced; destroyed right after
class Screen {
 public:
  virtual ~Screen() = default;

  virtual void OnEnter() {}
  virtual void OnExit() {}
  virtual void OnCovered() {}
  virtual void OnUncovered() {}

  // Only the top screen updates and gets input: the game freezes under a pause panel.
  virtual void FixedUpdate(float dt) {}
  virtual void Update(float dt) {}
  // Every *visible* screen draws, bottom to top (see IsOverlay).
  virtual void Render(float alpha) {}
  virtual void RenderUI(ui::UIContext& ui) {}  // draw with ui.canvas, widgets from ui/widgets.h
  virtual void DebugUI() {}  // top screen only (WLW_DEBUG_UI builds)

  // Android Back / desktop Escape, top screen first. Return true if handled (e.g. a pause
  // panel closes itself); false lets the screen below try, and in the end the app goes to the
  // background.
  virtual bool OnBack() { return false; }
  // The app went to the background (top screen only). A game screen typically pauses.
  virtual void OnAppPaused() {}

  // An overlay (pause, dialogs) doesn't cover the whole screen, so the screens below it are
  // drawn too - they just don't update.
  virtual bool IsOverlay() const { return false; }

  ScreenStack& Stack() const { return *stack_; }  // valid from OnEnter on

 private:
  friend class ScreenStack;
  ScreenStack* stack_ = nullptr;
};

// The stack of screens. Changes requested during a frame (Push/Pop/Replace/Reset) are queued
// and applied between frames by Application: a screen that pops itself in the middle of its own
// Update would otherwise be destroyed while still running.
//
// With Transition::Fade the change happens behind a fade to black: the screen fades out, the
// change is applied at full black, then it fades in. Input is ignored meanwhile.
class ScreenStack {
 public:
  enum class Transition { None, Fade };

  ScreenStack() = default;
  ScreenStack(const ScreenStack&) = delete;
  ScreenStack& operator=(const ScreenStack&) = delete;
  ~ScreenStack();

  void Push(std::unique_ptr<Screen> screen, Transition transition = Transition::None);
  void Pop(Transition transition = Transition::None);
  void Replace(std::unique_ptr<Screen> screen, Transition transition = Transition::None);  // the top one
  void Reset(std::unique_ptr<Screen> screen, Transition transition = Transition::None);    // all of them

  Screen* Top() const { return screens_.empty() ? nullptr : screens_.back().get(); }
  size_t Size() const { return screens_.size(); }
  bool Empty() const { return screens_.empty(); }
  bool Transitioning() const { return fade_phase_ != FadePhase::None; }
  float FadeAmount() const;  // 0 = clear, 1 = black (drawn by Application over the UI)
  float fade_time = 0.22f;   // seconds for each half of a fade

  // ---- Driven by Application each frame ----
  void FixedUpdate(float dt);
  void Update(float dt);
  void Render(float alpha);
  void RenderUI(ui::UIContext& ui);
  void DebugUI();
  bool Back();  // true if a screen handled it
  void AppPaused();
  void EndFrame(float dt);  // applies queued changes, advances fades
  void Clear();             // exits every screen now, drops queued changes (shutdown)

 private:
  enum class OpKind { Push, Pop, Replace, Reset };
  struct Op {
    OpKind kind;
    std::unique_ptr<Screen> screen;
    Transition transition;
  };
  enum class FadePhase { None, Out, In };

  void Queue(OpKind kind, std::unique_ptr<Screen> screen, Transition transition);
  void Apply(Op op);
  size_t FirstVisible() const;  // lowest screen that's drawn

  std::vector<std::unique_ptr<Screen>> screens_;
  std::vector<Op> pending_;
  FadePhase fade_phase_ = FadePhase::None;
  float fade_time_left_ = 0.0f;
};

}  // namespace wlw::app
