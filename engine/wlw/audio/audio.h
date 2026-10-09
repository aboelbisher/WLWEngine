#pragma once

#include <atomic>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

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

  // Background music: streamed, i.e. decoded bit by bit (on a background thread) while it plays, so a
  // long song costs its file size in memory instead of its decoded size (4 minutes of stereo is
  // ~40 MB as floats). Loops forever, on Bus::Music, crossfading from the previous track.
  // Playing the track that's already playing does nothing. False if the file can't be opened.
  bool PlayMusic(const std::string& path, float fade_seconds = 1.5f);
  void StopMusic(float fade_seconds = 1.0f) { mixer_->PlayStream(nullptr, fade_seconds); }

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
  struct MusicStream;  // a miniaudio decoder over a file in memory
  std::unique_ptr<Mixer> mixer_;
  std::unique_ptr<Device> device_;
  // Every music file opened so far, by path. Kept until the end (the mixer may still be reading
  // one); ~Audio stops the device before any of them is destroyed.
  std::map<std::string, std::unique_ptr<MusicStream>> music_;  // guarded by music_mutex_
  // The music decode thread (started by the first PlayMusic): keeps every stream's ring topped up.
  void DecodeLoop();
  std::mutex music_mutex_;
  std::thread decode_thread_;
  std::atomic<bool> decoding_{false};
};

}  // namespace wlw::audio
