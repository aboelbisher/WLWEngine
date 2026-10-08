#include "wlw/app/screen.h"

#include <algorithm>

#include "wlw/ui/canvas.h"

namespace wlw::app {

ScreenStack::~ScreenStack() { Clear(); }

void ScreenStack::Clear() {
  pending_.clear();
  fade_phase_ = FadePhase::None;
  // Exit from the top down, like popping them one by one.
  while (!screens_.empty()) {
    screens_.back()->OnExit();
    screens_.pop_back();
  }
}

void ScreenStack::Push(std::unique_ptr<Screen> screen, Transition transition) {
  Queue(OpKind::Push, std::move(screen), transition);
}
void ScreenStack::Pop(Transition transition) { Queue(OpKind::Pop, nullptr, transition); }
void ScreenStack::Replace(std::unique_ptr<Screen> screen, Transition transition) {
  Queue(OpKind::Replace, std::move(screen), transition);
}
void ScreenStack::Reset(std::unique_ptr<Screen> screen, Transition transition) {
  Queue(OpKind::Reset, std::move(screen), transition);
}

void ScreenStack::Queue(OpKind kind, std::unique_ptr<Screen> screen, Transition transition) {
  pending_.push_back({kind, std::move(screen), transition});
}

float ScreenStack::FadeAmount() const {
  if (fade_phase_ == FadePhase::None || fade_time <= 0.0f) return 0.0f;
  const float t = std::clamp(fade_time_left_ / fade_time, 0.0f, 1.0f);
  return fade_phase_ == FadePhase::Out ? 1.0f - t : t;
}

size_t ScreenStack::FirstVisible() const {
  size_t first = screens_.size();
  while (first > 0) {
    --first;
    if (!screens_[first]->IsOverlay()) break;  // an opaque screen hides everything below it
  }
  return first;
}

void ScreenStack::FixedUpdate(float dt) {
  if (Screen* top = Top(); top && !Transitioning()) top->FixedUpdate(dt);
}

void ScreenStack::Update(float dt) {
  if (Screen* top = Top(); top && !Transitioning()) top->Update(dt);
}

void ScreenStack::Render(float alpha) {
  for (size_t i = FirstVisible(); i < screens_.size(); ++i) screens_[i]->Render(alpha);
}

void ScreenStack::RenderUI(ui::Canvas& canvas) {
  for (size_t i = FirstVisible(); i < screens_.size(); ++i) {
    // Widgets (ui::Button) only respond on the top screen, and not during a fade.
    canvas.SetInteractive(i + 1 == screens_.size() && !Transitioning());
    screens_[i]->RenderUI(canvas);
  }
  canvas.SetInteractive(true);
}

void ScreenStack::DebugUI() {
  if (Screen* top = Top()) top->DebugUI();
}

bool ScreenStack::Back() {
  if (Transitioning()) return true;  // swallow it: something is already changing
  for (size_t i = screens_.size(); i > 0; --i) {
    if (screens_[i - 1]->OnBack()) return true;
  }
  return false;
}

void ScreenStack::AppPaused() {
  if (Screen* top = Top()) top->OnAppPaused();
}

void ScreenStack::EndFrame(float dt) {
  if (fade_phase_ != FadePhase::None) {
    fade_time_left_ -= dt;
    if (fade_time_left_ > 0.0f) return;
    if (fade_phase_ == FadePhase::Out) {
      // Fully black: make the change nobody sees, then fade back in.
      Apply(std::move(pending_.front()));
      pending_.erase(pending_.begin());
      fade_phase_ = FadePhase::In;
      fade_time_left_ = fade_time;
      return;
    }
    fade_phase_ = FadePhase::None;
  }
  while (!pending_.empty()) {
    if (pending_.front().transition == Transition::Fade && fade_time > 0.0f) {
      fade_phase_ = FadePhase::Out;  // applied when the fade-out ends
      fade_time_left_ = fade_time;
      return;
    }
    Op op = std::move(pending_.front());
    pending_.erase(pending_.begin());
    Apply(std::move(op));
  }
}

void ScreenStack::Apply(Op op) {
  auto pop = [this] {
    if (screens_.empty()) return;
    screens_.back()->OnExit();
    screens_.pop_back();
  };
  switch (op.kind) {
    case OpKind::Pop:
      pop();
      if (Screen* top = Top()) top->OnUncovered();
      return;
    case OpKind::Replace:
      pop();
      break;
    case OpKind::Reset:
      while (!screens_.empty()) pop();
      break;
    case OpKind::Push:
      if (Screen* top = Top()) top->OnCovered();
      break;
  }
  if (!op.screen) return;
  op.screen->stack_ = this;
  screens_.push_back(std::move(op.screen));
  screens_.back()->OnEnter();
}

}  // namespace wlw::app
