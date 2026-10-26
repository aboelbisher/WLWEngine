#include "wlw/audio/audio.h"

#include <algorithm>
#include <chrono>
#include <optional>
#include <vector>

#include "miniaudio.h"
#include "wlw/core/log.h"
#include "wlw/core/profile.h"
#include "wlw/platform/platform.h"

namespace wlw::audio {

struct Audio::Device {
  ma_device device{};
};

// A music file kept encoded in memory. Decoding is real work (Vorbis), too slow to risk on the
// audio thread, which must deliver every few milliseconds or the sound cuts out. So a decode
// thread (Audio::DecodeLoop) keeps ~1 s of decoded audio ready in a ring buffer, and the audio
// thread only copies from it.
//
// The ring is single-producer (decode thread) / single-consumer (audio thread): `written` and
// `read` count frames forever; only the producer moves `written`, only the consumer `read`.
//
// With an intro, the intro is decoded first and the same chunk carries on in the loop's decoder:
// the seam is sample-exact. (miniaudio's own chaining, ma_data_source_set_next, stops at the end of
// the chain: only the head's looping flag counts.)
struct Audio::MusicStream final : StreamSource {
  static constexpr size_t kChunk = 2048;  // frames decoded per step

  std::string path;      // the loop's file: which track this is
  bool retired = false;  // replaced by a newer playthrough of the same track: freed when not InUse
  Bytes bytes;           // the decoders read from here: must outlive them
  Bytes intro_bytes;
  ma_decoder decoder{};  // the loop
  ma_decoder intro{};
  bool ready = false;
  bool in_intro = false;  // intro decoder initialised and not finished
  bool has_intro = false;
  std::vector<float> ring;     // capacity * 2 floats (stereo)
  size_t capacity = 0;         // in frames
  std::vector<float> scratch;  // decode thread's buffer
  std::atomic<size_t> written{0};
  std::atomic<size_t> read{0};

  ~MusicStream() override {
    if (ready) ma_decoder_uninit(&decoder);
    if (has_intro) ma_decoder_uninit(&intro);
  }

  // Decode thread: top the ring up.
  void Fill() {
    size_t w = written.load(std::memory_order_relaxed);
    for (;;) {
      const size_t free = capacity - (w - read.load(std::memory_order_acquire));
      if (free < kChunk) return;
      ma_uint64 got = 0;
      if (in_intro) {
        ma_data_source_read_pcm_frames(&intro, scratch.data(), kChunk, &got);
        if (got < kChunk) in_intro = false;  // over: the rest of this chunk comes from the loop
      }
      if (!in_intro && got < kChunk) {
        // Through the data-source API, which handles looping (back to the start at the end).
        ma_uint64 more = 0;
        ma_data_source_read_pcm_frames(&decoder, scratch.data() + got * 2, kChunk - got, &more);
        got += more;
      }
      if (got == 0) return;
      for (size_t i = 0; i < got; ++i) {
        const size_t at = ((w + i) % capacity) * 2;
        ring[at] = scratch[i * 2];
        ring[at + 1] = scratch[i * 2 + 1];
      }
      w += got;
      written.store(w, std::memory_order_release);  // publishes the frames to the audio thread
    }
  }

  // Audio thread: copy out; if the decoder ever falls behind, play silence rather than stop.
  uint32_t Read(float* stereo, uint32_t frames) override {
    const size_t r = read.load(std::memory_order_relaxed);
    const size_t available = written.load(std::memory_order_acquire) - r;
    const size_t n = std::min<size_t>(frames, available);
    for (size_t i = 0; i < n; ++i) {
      const size_t at = ((r + i) % capacity) * 2;
      stereo[i * 2] = ring[at];
      stereo[i * 2 + 1] = ring[at + 1];
    }
    std::fill(stereo + n * 2, stereo + frames * 2, 0.0f);
    read.store(r + n, std::memory_order_release);  // frees the space for the decode thread
    return frames;                                 // music loops: it never ends
  }
};

void Audio::DecodeLoop() {
  WLW_PROFILE_THREAD_NAME("Audio decode");
  while (decoding_.load()) {
    {
      WLW_PROFILE_SCOPE("Decode music");
      std::lock_guard<std::mutex> lock(music_mutex_);
      for (auto& stream : music_) stream->Fill();
      FreeRetiredMusic();  // a restarted track's old playthrough, once it has faded out
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(10));  // the ring holds ~1 s: plenty of slack
  }
}

namespace {
constexpr uint32_t kFallbackSampleRate = 48000;

// Runs on the audio thread, every few milliseconds: fill `output` with the next `frames`.
void DataCallback(ma_device* device, void* output, const void* /*input*/, ma_uint32 frames) {
#if WLW_PROFILE
  static thread_local bool named = false;  // the device may restart on another thread
  if (!named) {
    WLW_PROFILE_THREAD_NAME("Audio callback");
    named = true;
  }
#endif
  WLW_PROFILE_SCOPE("Mix");
  static_cast<Mixer*>(device->pUserData)->Mix(static_cast<float*>(output), frames);
}
}  // namespace

Audio::Audio(const AudioConfig& config) {
  if (!config.enabled) {
    WLW_LOG(LogAudio, Info, "Audio off (AudioConfig::enabled): no device");
    mixer_ = std::make_unique<Mixer>(kFallbackSampleRate, config.max_voices);
    return;
  }
  auto device = std::make_unique<Device>();
  ma_device_config device_config = ma_device_config_init(ma_device_type_playback);
  device_config.playback.format = ma_format_f32;
  device_config.playback.channels = 2;
  device_config.sampleRate = 0;  // the device's native rate: no resampling at output time
  device_config.dataCallback = DataCallback;
  device_config.performanceProfile = ma_performance_profile_low_latency;  // small buffers: snappy sounds
  // Android: tell the OS this is game audio (routing, volume key behaviour).
  device_config.aaudio.usage = ma_aaudio_usage_game;
  device_config.aaudio.contentType = ma_aaudio_content_type_sonification;

  // The mixer must exist before the callback can run, and it needs the rate: open the device,
  // read its rate, create the mixer, hand it over, and only then start the device.
  if (ma_device_init(nullptr, &device_config, &device->device) != MA_SUCCESS) {
    WLW_LOG(LogAudio, Warning, "No audio device: the game runs without sound");
    mixer_ = std::make_unique<Mixer>(kFallbackSampleRate, config.max_voices);
    return;
  }
  mixer_ = std::make_unique<Mixer>(device->device.sampleRate, config.max_voices);
  device->device.pUserData = mixer_.get();
  if (ma_device_start(&device->device) != MA_SUCCESS) {
    WLW_LOG(LogAudio, Warning, "Audio device failed to start: no sound");
    ma_device_uninit(&device->device);
    return;
  }
  WLW_LOG(LogAudio, Info, "Audio: %s, %u Hz", ma_get_backend_name(device->device.pContext->backend),
          device->device.sampleRate);
  device_ = std::move(device);
}

Audio::~Audio() {
  // Stop the callback before the mixer it reads from goes away, then the decode thread.
  if (device_) ma_device_uninit(&device_->device);
  decoding_.store(false);
  if (decode_thread_.joinable()) decode_thread_.join();
}

Audio::SoundId Audio::Load(const std::string& path) {
  const std::optional<std::vector<uint8_t>> bytes = platform::ReadFile(path);
  if (!bytes) {
    WLW_LOG(LogAudio, Error, "Sound not found: %s", path.c_str());
    return kNoSound;
  }
  std::optional<SoundData> data = Decode(bytes->data(), bytes->size(), mixer_->SampleRate());
  if (!data) {
    WLW_LOG(LogAudio, Error, "Can't decode sound: %s", path.c_str());
    return kNoSound;
  }
  return mixer_->AddSound(std::move(*data));
}

Audio::Bytes Audio::MusicFile(const std::string& path) {
  auto it = music_files_.find(path);
  if (it != music_files_.end()) return it->second;
  std::optional<std::vector<uint8_t>> bytes = platform::ReadFile(path);
  if (!bytes) {
    WLW_LOG(LogAudio, Error, "Music not found: %s", path.c_str());
    return nullptr;
  }
  Bytes shared = std::make_shared<const std::vector<uint8_t>>(std::move(*bytes));
  music_files_.emplace(path, shared);
  return shared;
}

bool Audio::PlayMusic(const std::string& path, const MusicParams& params) {
  std::unique_lock<std::mutex> lock(music_mutex_);  // the decode thread walks music_
  // The playthrough to resume: the newest of this track (older ones are retired).
  MusicStream* stream = nullptr;
  for (auto& s : music_) {
    if (s->path == path && !s->retired) stream = s.get();
  }
  if (!stream || params.restart || !params.intro.empty()) {
    auto fresh = std::make_unique<MusicStream>();
    fresh->path = path;
    fresh->bytes = MusicFile(path);
    if (!fresh->bytes) return false;
    // Decoded straight to what the mixer wants: float, stereo, the device's rate (resampled).
    const ma_decoder_config config = ma_decoder_config_init(ma_format_f32, 2, mixer_->SampleRate());
    if (ma_decoder_init_memory(fresh->bytes->data(), fresh->bytes->size(), &config, &fresh->decoder) != MA_SUCCESS) {
      WLW_LOG(LogAudio, Error, "Can't decode music: %s", path.c_str());
      return false;
    }
    fresh->ready = true;
    ma_data_source_set_looping(&fresh->decoder, MA_TRUE);
    if (!params.intro.empty()) {
      fresh->intro_bytes = MusicFile(params.intro);
      if (fresh->intro_bytes && ma_decoder_init_memory(fresh->intro_bytes->data(), fresh->intro_bytes->size(), &config,
                                                       &fresh->intro) == MA_SUCCESS) {
        fresh->has_intro = fresh->in_intro = true;
      } else {
        WLW_LOG(LogAudio, Error, "Can't play the intro %s: the track starts without it", params.intro.c_str());
      }
    }
    fresh->capacity = mixer_->SampleRate();  // one second
    fresh->ring.assign(fresh->capacity * 2, 0.0f);
    fresh->scratch.assign(MusicStream::kChunk * 2, 0.0f);
    fresh->Fill();  // a head start, before the mixer reads (the decode thread may not be on it yet)
    if (stream) stream->retired = true;
    stream = fresh.get();
    music_.push_back(std::move(fresh));
  }
  FreeRetiredMusic();
  lock.unlock();
  if (!decode_thread_.joinable()) {
    decoding_.store(true);
    decode_thread_ = std::thread([this] { DecodeLoop(); });
  }
  mixer_->PlayStream(stream, params.fade, params.volume);
  return true;
}

void Audio::FreeRetiredMusic() {
  // Only retired playthroughs: nothing can hand them to the mixer again, so once the mixer lets go
  // (InUse false) the audio thread never reads them again.
  std::erase_if(music_, [](const std::unique_ptr<MusicStream>& s) { return s->retired && !s->InUse(); });
}

size_t Audio::MusicStreamCount() {
  std::lock_guard<std::mutex> lock(music_mutex_);
  return music_.size();
}

void Audio::Suspend() {
  suspended_ = true;
  UpdateDevice();
}

void Audio::Resume() {
  suspended_ = false;
  UpdateDevice();
}

void Audio::SetInterrupted(bool interrupted) {
  interrupted_ = interrupted;
  UpdateDevice();
}

void Audio::SetBusVolume(Bus bus, float volume) {
  if (bus == Bus::Music) {
    music_volume_ = volume;
    if (other_music_) return;  // applied when the other music stops
  }
  mixer_->SetBusVolume(bus, volume);
}

void Audio::SetOtherMusicPlaying(bool playing) {
  other_music_ = playing;
  mixer_->SetBusVolume(Bus::Music, playing ? 0.0f : music_volume_);
}

void Audio::UpdateDevice() {
  if (!device_) return;
  const bool play = !suspended_ && !interrupted_;
  if (play == (ma_device_is_started(&device_->device) != MA_FALSE)) return;
  if (play) {
    ma_device_start(&device_->device);
  } else {
    ma_device_stop(&device_->device);
  }
}

}  // namespace wlw::audio
