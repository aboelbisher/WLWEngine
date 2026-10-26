#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

#include "wlw/audio/filter.h"

namespace wlw::audio {

// A sound decoded to raw PCM: 32-bit float, stereo interleaved (L R L R ...), at the mixer's
// sample rate. Decoding happens once at load, so playing is just reading memory.
struct SoundData {
  std::vector<float> samples;
  uint32_t frames = 0;  // samples.size() / 2
};

// Decodes WAV, FLAC, MP3 or OGG Vorbis bytes to SoundData at `sample_rate` (resampled if needed).
std::optional<SoundData> Decode(const uint8_t* bytes, size_t size, uint32_t sample_rate);

enum class Bus : uint8_t { Effects, Music, Count };  // separately adjustable volumes

// Audio produced while it plays instead of decoded up front: music. Read() runs on the audio
// thread and must never block; it fills `frames` stereo frames (float, interleaved, at the
// mixer's rate) and returns how many it wrote (fewer = the stream ended).
class StreamSource {
 public:
  virtual ~StreamSource() = default;
  virtual uint32_t Read(float* stereo, uint32_t frames) = 0;
  // True while the mixer may still read this source: a PlayStream naming it is queued, or one of
  // its stream slots holds it (playing or fading out). Its owner may destroy it once this is false,
  // as long as it doesn't hand it to PlayStream again.
  bool InUse() const { return queued_.load() > 0 || in_slot_.load(); }

 private:
  friend class Mixer;
  std::atomic<int> queued_{0};        // PlayStream commands not yet taken by the audio thread
  std::atomic<bool> in_slot_{false};  // written by the audio thread
};

struct PlayParams {
  float volume = 1.0f;
  float pitch = 1.0f;  // playback speed: 2 = an octave up (and twice as short)
  float pan = 0.0f;    // -1 left .. +1 right
  Bus bus = Bus::Effects;
  bool loop = false;
};

// Ducking: a bus dips while something important plays over it (music under a boom or a voice line),
// then comes back. The level moves in dB at an even speed: down to `depth_db` over `attack`, stays
// there `hold`, back up to 0 dB over `release` (seconds).
struct DuckParams {
  float depth_db = -6.0f;
  float attack = 0.03f;
  float hold = 0.3f;
  float release = 0.5f;
};

// A bus's live state as of the last Mix() (debug panels, tests).
struct BusLevels {
  float low_pass_hz = 0.0f;
  float high_pass_hz = 0.0f;
  float gain_db = 0.0f;
  float duck_db = 0.0f;
};

// Mixes all playing sounds into the output buffer. Two threads use it:
//   game thread:  AddSound, Play, StopAll, volumes  (never blocks)
//   audio thread: Mix, called by the OS every few milliseconds for the next block of samples
// The audio thread must never wait (a late buffer is an audible click), so Play() doesn't touch
// the voices directly: it drops a command into a lock-free ring buffer that Mix() drains.
//
// Each bus is mixed into its own buffer first, then shaped (low-pass, high-pass, gain, ducking: see
// "Live mix") and added to the output. A bus with nothing set passes through untouched.
class Mixer {
 public:
  using SoundId = int;
  static constexpr SoundId kNoSound = -1;

  explicit Mixer(uint32_t sample_rate, int max_voices = 24);

  SoundId AddSound(SoundData data);  // game thread; the data lives as long as the mixer
  void Play(SoundId sound, const PlayParams& params = {});
  void StopAll();
  // Streams `source` on Bus::Music at `volume`, fading in over `fade_seconds`; the stream playing
  // before fades out over the same time (a crossfade). Fades are equal-power (gain = volume x
  // sin(t x pi/2), t going 0..1), so a crossfade between equally loud tracks keeps the loudness level.
  // A source still fading out is picked up again from where its fade is (no cut, no restart); the
  // one already playing only takes the new volume. nullptr fades the music out. Several fades can
  // overlap (kStreamSlots - 1 fading at once; past that the quietest is dropped). The source must
  // stay alive while InUse().
  void PlayStream(StreamSource* source, float fade_seconds, float volume = 1.0f);
  static constexpr size_t kStreamSlots = 4;  // the current stream + the ones fading out

  // ---- Settings (game thread; read by the audio thread) ----
  void SetMasterVolume(float volume) { master_.store(volume); }
  void SetBusVolume(Bus bus, float volume) { bus_volume_[static_cast<size_t>(bus)].store(volume); }
  void SetMuted(bool muted) { muted_.store(muted); }
  bool Muted() const { return muted_.load(); }
  // At most this many copies of one sound at once; a new one replaces the oldest copy. Keeps a
  // 15-block line clear from becoming 15 stacked pops.
  int max_instances_per_sound = 4;

  // ---- Live mix (game thread, through the command queue; glides and envelopes run on the audio thread) ----
  // A bus's tone and level, changed smoothly while it plays (state the game sets each moment, like a
  // snapshot: "paused" = muffled and quieter). Cutoffs glide in octaves (Filter); gains in dB.
  // Filter::kHighestHz switches the low-pass off, Filter::kLowestHz the high-pass.
  void SetBusLowPass(Bus bus, float hz, float glide_seconds = 0.3f);
  void SetBusHighPass(Bus bus, float hz, float glide_seconds = 0.3f);
  void SetBusGain(Bus bus, float db, float glide_seconds = 0.3f);  // on top of the bus volume
  // Dips the bus (DuckParams). Ducks overlap by depth: the deepest one going on wins, and a duck
  // arriving during another one's hold extends the hold.
  void Duck(Bus bus, const DuckParams& params = {});
  BusLevels Levels(Bus bus) const;

  // ---- Audio thread ----
  void Mix(float* out, uint32_t frames);  // writes frames * 2 floats (stereo)

  uint32_t SampleRate() const { return sample_rate_; }
  int ActiveVoices() const { return active_voices_.load(); }  // as of the last Mix()

 private:
  struct Command {
    enum class Kind : uint8_t { Play, StopAll, PlayStream, BusLowPass, BusHighPass, BusGain, Duck } kind = Kind::Play;
    const SoundData* sound = nullptr;
    PlayParams params;
    StreamSource* stream = nullptr;  // PlayStream
    float fade_seconds = 0.0f;
    float stream_volume = 1.0f;
    Bus bus = Bus::Effects;  // the live-mix commands: which bus, the new value (Hz or dB), the glide time
    float value = 0.0f;
    float glide = 0.0f;
    DuckParams duck;
  };
  struct Voice {
    const SoundData* sound = nullptr;  // null = free
    double position = 0.0;             // in frames; fractional because of pitch
    double step = 1.0;                 // frames advanced per output frame (= pitch)
    float gain_left = 1.0f;
    float gain_right = 1.0f;
    Bus bus = Bus::Effects;
    bool loop = false;
    uint64_t started = 0;  // play order, to find the oldest voice
  };

  bool Push(const Command& command);  // false if the queue is full (the play is dropped)
  void StartVoice(const Command& command);
  void StartStream(const Command& command);
  void MixStreams(float* out, uint32_t frames, float master);
  void MixBlock(float* out, uint32_t frames);  // at most kBlock frames
  void ApplyBusCommand(const Command& command);

  // A level in dB moving at an even speed toward a target (the bus gain).
  struct Glide {
    float db = 0.0f;
    float target = 0.0f;
    float step = 0.0f;  // dB per frame, > 0
    void Set(float to, float frames);
    void Advance();
  };
  // The duck: down at `attack` dB per frame, held at the bottom `hold` frames, back up at `release`.
  struct DuckEnvelope {
    float db = 0.0f;
    float target = 0.0f;  // the bottom while ducking, 0 after
    float attack = 0.0f;
    float release = 0.0f;
    uint32_t hold = 0;
    void Start(const DuckParams& params, float sample_rate);
    void Advance();
    bool Idle() const { return db == 0.0f && target == 0.0f; }
  };
  struct BusState {
    Filter low_pass, high_pass;
    Glide gain;
    DuckEnvelope duck;
  };
  void ShapeBus(BusState& bus, float* samples, uint32_t frames);
  static constexpr uint32_t kBlock = 512;                        // frames mixed per bus buffer pass
  std::array<BusState, static_cast<size_t>(Bus::Count)> buses_;  // audio thread
  std::vector<float> bus_mix_;                                   // kBlock stereo frames per bus, allocated once
  struct LevelsOut {
    std::atomic<float> low_pass{0.0f}, high_pass{0.0f}, gain{0.0f}, duck{0.0f};
  };
  std::array<LevelsOut, static_cast<size_t>(Bus::Count)> levels_;  // written by the audio thread

  // A streaming source and where it is in its fade: t = 0 silent .. 1 fully in; the gain is
  // volume x sin(t x pi/2). t moves by `step` per frame (up = fading in, down = fading out).
  struct StreamSlot {
    StreamSource* source = nullptr;
    float t = 0.0f;
    float step = 0.0f;
    float volume = 1.0f;
    float Gain() const;
  };
  void FreeSlot(StreamSlot& slot);
  // [0] = current, the others fading out (audio thread).
  std::array<StreamSlot, kStreamSlots> streams_;
  std::vector<float> stream_scratch_;             // decode buffer, allocated once (never in Mix)
  static constexpr uint32_t kStreamChunk = 1024;  // frames decoded per step

  uint32_t sample_rate_;
  std::vector<std::unique_ptr<SoundData>> sounds_;  // owned; voices point into them
  std::vector<Voice> voices_;                       // audio thread only
  uint64_t play_counter_ = 0;

  // Single-producer single-consumer ring buffer: the game thread writes at head_, the audio
  // thread reads at tail_. Each index is written by one thread only, so two atomics suffice.
  static constexpr size_t kQueueSize = 128;
  std::array<Command, kQueueSize> queue_;
  std::atomic<size_t> head_{0};
  std::atomic<size_t> tail_{0};

  std::atomic<float> master_{1.0f};
  std::array<std::atomic<float>, static_cast<size_t>(Bus::Count)> bus_volume_{};
  std::atomic<bool> muted_{false};
  std::atomic<int> active_voices_{0};
};

}  // namespace wlw::audio
