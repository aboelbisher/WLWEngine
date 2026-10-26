#include "wlw/audio/filter.h"

#include <algorithm>
#include <cmath>

namespace wlw::audio {

Filter::Filter(Kind kind, float sample_rate, float resonance)
    : kind_(kind), sample_rate_(sample_rate), damping_(1.0f / std::max(resonance, 0.1f)) {
  min_octave_ = std::log2(kLowestHz);
  max_octave_ = std::log2(std::min(kHighestHz, 0.45f * sample_rate));
  octave_ = target_octave_ = OpenOctave();
  UpdateCoefficients();
}

void Filter::SetCutoff(float hz, float seconds) {
  target_octave_ = std::clamp(std::log2(std::max(hz, 1.0f)), min_octave_, max_octave_);
  const float frames = seconds * sample_rate_;
  if (frames < 1.0f) {
    octave_ = target_octave_;
    glide_frames_ = 0;
    UpdateCoefficients();
    return;
  }
  glide_frames_ = static_cast<uint32_t>(frames);
}

float Filter::Cutoff() const { return std::exp2(octave_); }

float Filter::TargetCutoff() const { return std::exp2(target_octave_); }

bool Filter::Open() const { return glide_frames_ == 0 && octave_ == OpenOctave(); }

void Filter::Reset() {
  ic1_ = {};
  ic2_ = {};
  running_ = false;
}

void Filter::UpdateCoefficients() {
  // g = tan(pi fc / fs) is the integrators' gain, pre-warped so the cutoff lands where asked (the
  // trapezoidal rule bends the frequency axis near Nyquist); the a's solve the loop for one sample.
  const float g = std::tan(3.14159265f * std::exp2(octave_) / sample_rate_);
  a1_ = 1.0f / (1.0f + g * (g + damping_));
  a2_ = g * a1_;
  a3_ = g * a2_;
}

void Filter::Process(float* stereo, uint32_t frames) {
  if (Open()) {
    running_ = false;
    return;
  }
  if (!running_ && frames > 0) {
    // Coming out of bypass, fully open: start the integrators where they'd be for this input, so
    // the first samples don't step. A low-pass's second integrator holds its output (= the input,
    // when open); a high-pass at its lowest cutoff starts from rest (its output is the input).
    ic1_ = {};
    ic2_ = {};
    if (kind_ == Kind::LowPass) ic2_ = {stereo[0], stereo[1]};
    running_ = true;
  }
  for (uint32_t i = 0; i < frames; ++i) {
    if (glide_frames_ > 0) {  // an even share of what's left each frame: lands exactly on time
      octave_ += (target_octave_ - octave_) / static_cast<float>(glide_frames_);
      if (--glide_frames_ == 0) octave_ = target_octave_;
      UpdateCoefficients();
    }
    for (int c = 0; c < 2; ++c) {
      float& x = stereo[i * 2 + c];
      const float v3 = x - ic2_[c];
      const float v1 = a1_ * ic1_[c] + a2_ * v3;            // band-pass
      const float v2 = ic2_[c] + a2_ * ic1_[c] + a3_ * v3;  // low-pass
      ic1_[c] = 2.0f * v1 - ic1_[c];
      ic2_[c] = 2.0f * v2 - ic2_[c];
      x = kind_ == Kind::LowPass ? v2 : x - damping_ * v1 - v2;
    }
  }
  // After silence the states decay into denormals, which are very slow on many CPUs: flush them.
  for (int c = 0; c < 2; ++c) {
    if (std::fabs(ic1_[c]) < 1e-20f) ic1_[c] = 0.0f;
    if (std::fabs(ic2_[c]) < 1e-20f) ic2_[c] = 0.0f;
  }
}

}  // namespace wlw::audio
