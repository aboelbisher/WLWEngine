#pragma once

#include <array>
#include <cstdint>

namespace wlw::audio {

// A two-pole (12 dB per octave) low-pass or high-pass filter that can be swept while it plays: music
// muffled under a pause menu, opening up as a combo grows, the world going dull in slow motion.
//
// It's a state-variable filter in its "topology-preserving transform" form (Zavalishin, "The Art of
// VA Filter Design"; Simper's SVF): two integrators simulated with the trapezoidal rule. The textbook
// biquad (RBJ cookbook) sounds the same while the cutoff stays put, but its coefficients mix the
// cutoff into the stored past samples: changing them every sample makes zipper noise, clicks, and at
// fast sweeps it can blow up. The SVF's state is two integrator levels that mean the same thing at
// any cutoff, so it stays smooth and stable however fast the cutoff moves.
//
// The cutoff glides at an even speed in octaves (log2 Hz), because pitch is heard that way: 400 Hz ->
// 800 Hz sounds as big a step as 4 kHz -> 8 kHz. Coefficients are recomputed every sample while it
// glides (no steps to hear). All the way open (kHighestHz for a low-pass, kLowestHz for a high-pass)
// and settled, Process leaves the sound untouched and costs nothing.
//
// Audio-thread object: no locks, no allocation. Stereo interleaved floats, like the Mixer.
class Filter {
 public:
  enum class Kind : uint8_t { LowPass, HighPass };
  static constexpr float kLowestHz = 20.0f;
  static constexpr float kHighestHz = 20000.0f;  // (capped at 0.45 x the sample rate)

  Filter() = default;
  // `resonance` is Q: 0.707 is the flattest response (Butterworth); higher rings at the cutoff.
  Filter(Kind kind, float sample_rate, float resonance = 0.7071f);

  // Glides to `hz` over `seconds` (0: jumps). A new call starts a new glide from where it is.
  void SetCutoff(float hz, float seconds);
  float Cutoff() const;  // now, in Hz
  float TargetCutoff() const;
  bool Open() const;  // all the way open and settled: Process does nothing
  void Process(float* stereo, uint32_t frames);
  void Reset();  // forgets the past (the next Process starts from silence)

 private:
  float OpenOctave() const { return kind_ == Kind::LowPass ? max_octave_ : min_octave_; }
  void UpdateCoefficients();

  Kind kind_ = Kind::LowPass;
  float sample_rate_ = 48000.0f;
  float damping_ = 1.4142f;  // k = 1 / Q
  float min_octave_ = 0.0f, max_octave_ = 0.0f;
  float octave_ = 0.0f;         // log2 of the cutoff now...
  float target_octave_ = 0.0f;  // ...and where it's going
  uint32_t glide_frames_ = 0;   // frames left in the glide
  float a1_ = 0.0f, a2_ = 0.0f, a3_ = 0.0f;
  std::array<float, 2> ic1_{}, ic2_{};  // the two integrators' states, per channel
  bool running_ = false;                // the state follows the signal (false while open: bypassed)
};

}  // namespace wlw::audio
