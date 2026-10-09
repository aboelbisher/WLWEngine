#include "wlw/audio/audio.h"

#include "miniaudio.h"
#include "wlw/core/log.h"
#include "wlw/platform/platform.h"

namespace wlw::audio {

struct Audio::Device {
  ma_device device{};
};

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
  // Stop the callback before the mixer it reads from goes away.
  if (device_) ma_device_uninit(&device_->device);
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

void Audio::Suspend() {
  if (device_) ma_device_stop(&device_->device);
}

void Audio::Resume() {
  if (device_) ma_device_start(&device_->device);
}

}  // namespace wlw::audio
