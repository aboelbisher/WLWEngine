#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

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
};

struct PlayParams {
  float volume = 1.0f;
  float pitch = 1.0f;  // playback speed: 2 = an octave up (and twice as short)
  float pan = 0.0f;    // -1 left .. +1 right
  Bus bus = Bus::Effects;
  bool loop = false;
};

// Mixes all playing sounds into the output buffer. Two threads use it:
//   game thread:  AddSound, Play, StopAll, volumes  (never blocks)
//   audio thread: Mix, called by the OS every few milliseconds for the next block of samples
// The audio thread must never wait (a late buffer is an audible click), so Play() doesn't touch
// the voices directly: it drops a command into a lock-free ring buffer that Mix() drains.
class Mixer {
 public:
  using SoundId = int;
  static constexpr SoundId kNoSound = -1;

  explicit Mixer(uint32_t sample_rate, int max_voices = 24);

  SoundId AddSound(SoundData data);  // game thread; the data lives as long as the mixer
  void Play(SoundId sound, const PlayParams& params = {});
  void StopAll();
  // Streams `source` on Bus::Music, fading in over `fade_seconds`; the stream playing before fades
  // out over the same time (a crossfade). nullptr fades the music out. The source must outlive
  // its playing (Audio keeps the music streams for its whole life).
  void PlayStream(StreamSource* source, float fade_seconds);

  // ---- Settings (game thread; read by the audio thread) ----
  void SetMasterVolume(float volume) { master_.store(volume); }
  void SetBusVolume(Bus bus, float volume) { bus_volume_[static_cast<size_t>(bus)].store(volume); }
  void SetMuted(bool muted) { muted_.store(muted); }
  bool Muted() const { return muted_.load(); }
  // At most this many copies of one sound at once; a new one replaces the oldest copy. Keeps a
  // 15-block line clear from becoming 15 stacked pops.
  int max_instances_per_sound = 4;

  // ---- Audio thread ----
  void Mix(float* out, uint32_t frames);  // writes frames * 2 floats (stereo)

  uint32_t SampleRate() const { return sample_rate_; }
  int ActiveVoices() const { return active_voices_.load(); }  // as of the last Mix()

 private:
  struct Command {
    enum class Kind : uint8_t { Play, StopAll, PlayStream } kind = Kind::Play;
    const SoundData* sound = nullptr;
    PlayParams params;
    StreamSource* stream = nullptr;  // PlayStream
    float fade_seconds = 0.0f;
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

  // A streaming source with a gain moving linearly towards a target (fades without clicks).
  struct StreamSlot {
    StreamSource* source = nullptr;
    float gain = 0.0f;
    float target = 0.0f;
    float step = 0.0f;  // gain change per frame
  };
  std::array<StreamSlot, 2> streams_;  // [0] = current, [1] = the one fading out (audio thread)
  std::vector<float> stream_scratch_;   // decode buffer, allocated once (never in Mix)
  static constexpr uint32_t kStreamChunk = 1024;  // frames decoded per step

  uint32_t sample_rate_;
  std::vector<std::unique_ptr<SoundData>> sounds_;  // owned; voices point into them
  std::vector<Voice> voices_;                        // audio thread only
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
