#pragma once

#include <algorithm>
#include <vector>

namespace wlw::anim {

// A sequence of timed beats on one clock: "the panel drops in, then the score counts up, with a
// tick sound at its start, then the stats". Instead of chaining delays by hand (each tween's delay
// = the sum of everything before it), beats are placed relative to each other, and moving or
// lengthening one moves everything after it.
//
// Like Tween it's a plain value: the owner calls Update(dt) and asks about beats. There are no
// callbacks; a cue (a sound, a haptic) is a beat of length 0 that the owner polls with Crossed(),
// which is true on the one update that reached it, even when a long frame (or Skip) jumps past
// several beats at once.
//
//   Timeline t;
//   const auto panel = t.Then(0.35f);         // 0.00 .. 0.35
//   const auto count = t.Then(0.8f, 0.1f);    // 0.45 .. 1.25 (a 0.1 s gap)
//   const auto tick = t.With(0.0f);           // a cue at 0.45, the count's start
//   t.Update(dt);
//   if (t.Crossed(tick)) PlaySound();
//   score = Lerp(0, final, t.Progress(count));
//
// Skip() jumps to the end (a tap to see the result at once); Skipped() tells the owner, so it can
// drop the cues that only make sense one by one (ticks) and keep the ones that matter (a fanfare).
class Timeline {
 public:
  using Beat = int;  // a beat's handle: its index, in the order added

  // A beat starting `gap` seconds after the end of the last beat added.
  Beat Then(float duration, float gap = 0.0f) {
    const float start = beats_.empty() ? 0.0f : beats_.back().start + beats_.back().duration;
    return At(start + gap, duration);
  }
  // A beat starting `offset` seconds after the start of the last beat added (together with it).
  Beat With(float duration, float offset = 0.0f) {
    const float start = beats_.empty() ? 0.0f : beats_.back().start;
    return At(start + offset, duration);
  }
  // A beat at an absolute time on the timeline.
  Beat At(float start, float duration) {
    beats_.push_back({std::max(start, 0.0f), std::max(duration, 0.0f)});
    return static_cast<Beat>(beats_.size()) - 1;
  }

  void Update(float dt) { Advance(time_ + dt); }
  // Jumps to the end: every beat is done, and the ones not reached yet are Crossed() now.
  void Skip() {
    Advance(std::max(time_, Duration()));
    skipped_ = true;
  }
  // Back to the start, beats kept (Clear() drops them too).
  void Restart() {
    time_ = previous_ = 0.0f;
    updated_ = skipped_ = false;
  }
  void Clear() {
    beats_.clear();
    Restart();
  }

  float Time() const { return time_; }
  float Duration() const {  // the end of the last beat to end
    float end = 0.0f;
    for (const Span& span : beats_) end = std::max(end, span.start + span.duration);
    return end;
  }
  bool Finished() const { return time_ >= Duration(); }
  bool Skipped() const { return skipped_; }

  float Start(Beat beat) const { return Get(beat).start; }
  float End(Beat beat) const { return Get(beat).start + Get(beat).duration; }
  // Linear progress through the beat in [0, 1] (0 before it, 1 after it; a cue jumps 0 -> 1).
  float Progress(Beat beat) const {
    const Span& span = Get(beat);
    if (span.duration <= 0.0f) return time_ >= span.start ? 1.0f : 0.0f;
    return std::clamp((time_ - span.start) / span.duration, 0.0f, 1.0f);
  }
  // Seconds since the beat started (0 before it, not capped): for looping effects inside it.
  float Elapsed(Beat beat) const { return std::max(time_ - Get(beat).start, 0.0f); }
  bool Started(Beat beat) const { return time_ >= Get(beat).start; }
  bool Done(Beat beat) const { return time_ >= End(beat); }
  // True on the update that reached the beat's start (once). The first update counts time 0.
  bool Crossed(Beat beat) const {
    const float start = Get(beat).start;
    return updated_ && start > previous_ && start <= time_;
  }

 private:
  struct Span {
    float start = 0.0f;
    float duration = 0.0f;
  };
  const Span& Get(Beat beat) const {
    static const Span kNone{};
    return beat >= 0 && beat < static_cast<Beat>(beats_.size()) ? beats_[static_cast<size_t>(beat)] : kNone;
  }
  void Advance(float to) {
    previous_ = updated_ ? time_ : -1.0f;  // the first update also crosses the beats at 0
    updated_ = true;
    time_ = to;
  }

  std::vector<Span> beats_;
  float time_ = 0.0f;
  float previous_ = 0.0f;  // the time before the last update: (previous_, time_] was just crossed
  bool updated_ = false;
  bool skipped_ = false;
};

}  // namespace wlw::anim
