#pragma once

#include <memory>
#include <string>

#include "wlw/audio/mixer.h"

namespace wlw::audio {

// The game's sound output: one playback device (miniaudio: WASAPI on Windows, AAudio/OpenSL ES on
// Android...) whose callback pulls mixed samples from a Mixer. Owned by Application.
//
//   SoundId pop = Audio().Load("sounds/pop.ogg");   // decode once, at startup
//   Audio().Play(pop, {.volume = 0.8f, .pitch = 1.1f});
//
// If no device can be opened the game runs silently (logged), it doesn't fail.
class Audio {
 public:
  using SoundId = Mixer::SoundId;
  static constexpr SoundId kNoSound = Mixer::kNoSound;

  Audio();
  ~Audio();
  Audio(const Audio&) = delete;
  Audio& operator=(const Audio&) = delete;

  // Reads (platform::ReadFile, so APK assets work) and decodes a WAV/OGG/MP3/FLAC file.
  SoundId Load(const std::string& path);
  void Play(SoundId sound, const PlayParams& params = {}) { mixer_->Play(sound, params); }
  void StopAll() { mixer_->StopAll(); }

  void SetMuted(bool muted) { mixer_->SetMuted(muted); }
  bool Muted() const { return mixer_->Muted(); }
  void SetMasterVolume(float volume) { mixer_->SetMasterVolume(volume); }
  void SetBusVolume(Bus bus, float volume) { mixer_->SetBusVolume(bus, volume); }

  // Stops the device while the app is in the background (no sound, no CPU) and restarts it.
  void Suspend();
  void Resume();

  bool HasDevice() const { return device_ != nullptr; }
  Mixer& GetMixer() { return *mixer_; }

 private:
  struct Device;  // miniaudio's device, kept out of this header
  std::unique_ptr<Mixer> mixer_;
  std::unique_ptr<Device> device_;
};

}  // namespace wlw::audio
