#include "wlw/studio/play_session.h"

#include "wlw/studio/studio_log.h"
#include "wlw/core/profile.h"

namespace wlw::studio {

bool PlaySession::Play(const DeviceScreen& screen) {
  Stop();
  last_exit_code_.reset();
  if (!CanPlay()) return false;
  WLW_PROFILE_SCOPE("Play");
  window_ = scene::OffscreenWindow::Create({.size = screen.size, .safe_area = screen.safe_area, .touch = screen.touch});
  game_ = make_game_();
  if (game_ == nullptr || !game_->Internal().InitHosted(studio_, window_)) {
    WLW_LOG(LogStudio, Error, "Studio: the game didn't start");
    game_.reset();
    window_.reset();
    return false;
  }
  WLW_LOG(LogStudio, Info, "Studio: playing at %d x %d%s", screen.size.x, screen.size.y, screen.touch ? " (touch)" : "");
  return true;
}

void PlaySession::Stop() {
  if (!game_) return;
  WLW_PROFILE_SCOPE("Stop");
  game_->Internal().Shutdown();
  game_.reset();  // before its window and before the Studio: a hosted app goes first
  window_.reset();
  WLW_LOG(LogStudio, Info, "Studio: stopped");
}

void PlaySession::SetPaused(bool paused) {
  if (game_) game_->Internal().SetPaused(paused);
}

bool PlaySession::Paused() const { return game_ && game_->Internal().IsPaused(); }

void PlaySession::Step() {
  if (game_) game_->Internal().Step();
}

void PlaySession::Frame() {
  if (!game_) return;
  WLW_PROFILE_SCOPE("Game frame");
  if (game_->Internal().Frame()) return;
  // The game quit (or Escape had nothing left to close): the session is over.
  const int code = game_->Internal().Shutdown();
  game_.reset();
  window_.reset();
  last_exit_code_ = code;
  WLW_LOG(LogStudio, Info, "Studio: the game quit (exit code %d)", code);
}

void PlaySession::SetScreen(const DeviceScreen& screen) {
  if (!window_) return;
  window_->SetSize(screen.size);
  window_->SetSafeAreaInsets(screen.safe_area);
  window_->SetTouchScreen(screen.touch);
}

}  // namespace wlw::studio
