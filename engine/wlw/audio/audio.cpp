#include "wlw/audio/audio.h"

#include <algorithm>
#include <chrono>
#include <optional>
#include <vector>

#include "miniaudio.h"
#include "wlw/core/log.h"
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
struct Audio::MusicStream final : StreamSource {
  static constexpr size_t kChunk = 2048;  // frames decoded per step

  std::vector<uint8_t> bytes;  // the decoder reads from here: must outlive it
  ma_decoder decoder{};
  bool ready = false;
  std::vector<float> ring;     // capacity * 2 floats (stereo)
  size_t capacity = 0;         // in frames
  std::vector<float> scratch;  // decode thread's buffer
  std::atomic<size_t> written{0};
  std::atomic<size_t> read{0};

  ~MusicStream() override {
    if (ready) ma_decoder_uninit(&decoder);
  }

  // Decode thread: top the ring up.
  void Fill() {
    size_t w = written.load(std::memory_order_relaxed);
    for (;;) {
      const size_t free = capacity - (w - read.load(std::memory_order_acquire));
      if (free < kChunk) return;
      ma_uint64 got = 0;
      // Through the data-source API, which handles looping (back to the start at the end).
      ma_data_source_read_pcm_frames(&decoder, scratch.data(), kChunk, &got);
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
    return frames;  // music loops: it never ends
  }
};

void Audio::DecodeLoop() {
  while (decoding_.load()) {
    {
      std::lock_guard<std::mutex> lock(music_mutex_);
      for (auto& [path, stream] : music_) stream->Fill();
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(10));  // the ring holds ~1 s: plenty of slack
  }
}

namespace {
constexpr uint32_t kFallbackSampleRate = 48000;

// Runs on the audio thread, every few milliseconds: fill `output` with the next `frames`.
void DataCallback(ma_device* device, void* output, const void* /*input*/, ma_uint32 frames) {
  static_cast<Mixer*>(device->pUserData)->Mix(static_cast<float*>(output), frames);
}
}  // namespace

Audio::Audio() {
  auto device = std::make_unique<Device>();
  ma_device_config config = ma_device_config_init(ma_device_type_playback);
  config.playback.format = ma_format_f32;
  config.playback.channels = 2;
  config.sampleRate = 0;  // the device's native rate: no resampling at output time
  config.dataCallback = DataCallback;
  config.performanceProfile = ma_performance_profile_low_latency;  // small buffers: snappy sounds
  // Android: tell the OS this is game audio (routing, volume key behaviour).
  config.aaudio.usage = ma_aaudio_usage_game;
  config.aaudio.contentType = ma_aaudio_content_type_sonification;

  // The mixer must exist before the callback can run, and it needs the rate: open the device,
  // read its rate, create the mixer, hand it over, and only then start the device.
  if (ma_device_init(nullptr, &config, &device->device) != MA_SUCCESS) {
    WLW_LOG_WARN("No audio device: the game runs without sound");
    mixer_ = std::make_unique<Mixer>(kFallbackSampleRate);
    return;
  }
  mixer_ = std::make_unique<Mixer>(device->device.sampleRate);
  device->device.pUserData = mixer_.get();
  if (ma_device_start(&device->device) != MA_SUCCESS) {
    WLW_LOG_WARN("Audio device failed to start: no sound");
    ma_device_uninit(&device->device);
    return;
  }
  WLW_LOG_INFO("Audio: %s, %u Hz", ma_get_backend_name(device->device.pContext->backend), device->device.sampleRate);
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
    WLW_LOG_ERROR("Sound not found: %s", path.c_str());
    return kNoSound;
  }
  std::optional<SoundData> data = Decode(bytes->data(), bytes->size(), mixer_->SampleRate());
  if (!data) {
    WLW_LOG_ERROR("Can't decode sound: %s", path.c_str());
    return kNoSound;
  }
  return mixer_->AddSound(std::move(*data));
}

bool Audio::PlayMusic(const std::string& path, float fade_seconds) {
  std::unique_lock<std::mutex> lock(music_mutex_);  // the decode thread walks music_
  auto it = music_.find(path);
  if (it == music_.end()) {
    std::optional<std::vector<uint8_t>> bytes = platform::ReadFile(path);
    if (!bytes) {
      WLW_LOG_ERROR("Music not found: %s", path.c_str());
      return false;
    }
    auto stream = std::make_unique<MusicStream>();
    stream->bytes = std::move(*bytes);
    // Decoded straight to what the mixer wants: float, stereo, the device's rate (resampled).
    const ma_decoder_config config = ma_decoder_config_init(ma_format_f32, 2, mixer_->SampleRate());
    if (ma_decoder_init_memory(stream->bytes.data(), stream->bytes.size(), &config, &stream->decoder) != MA_SUCCESS) {
      WLW_LOG_ERROR("Can't decode music: %s", path.c_str());
      return false;
    }
    stream->ready = true;
    ma_data_source_set_looping(&stream->decoder, MA_TRUE);
    stream->capacity = mixer_->SampleRate();  // one second
    stream->ring.assign(stream->capacity * 2, 0.0f);
    stream->scratch.assign(MusicStream::kChunk * 2, 0.0f);
    stream->Fill();  // a head start, before the mixer reads (the decode thread isn't on it yet)
    it = music_.emplace(path, std::move(stream)).first;
  }
  StreamSource* source = it->second.get();
  lock.unlock();
  if (!decode_thread_.joinable()) {
    decoding_.store(true);
    decode_thread_ = std::thread([this] { DecodeLoop(); });
  }
  mixer_->PlayStream(source, fade_seconds);
  return true;
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
  if (play == ma_device_is_started(&device_->device)) return;
  if (play) {
    ma_device_start(&device_->device);
  } else {
    ma_device_stop(&device_->device);
  }
}

}  // namespace wlw::audio
