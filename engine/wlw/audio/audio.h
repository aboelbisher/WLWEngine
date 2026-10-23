#pragma once

#include <atomic>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

#include "wlw/audio/audio_config.h"
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

  explicit Audio(const AudioConfig& config = {});
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
  void SetBusVolume(Bus bus, float volume);

  // Stops the device while the app is in the background (no sound, no CPU) and restarts it.
  void Suspend();
  void Resume();
  // Something that must be heard alone is going on while we're on screen: a phone call, an alarm,
  // the voice assistant. Silent until it ends. Independent of Suspend: the device plays only when
  // it's neither suspended nor interrupted.
  void SetInterrupted(bool interrupted);
  bool Interrupted() const { return interrupted_; }
  // The player's own music (another app) is playing: ours steps aside (Bus::Music silent, its
  // volume kept for when that music stops); sound effects still mix over it.
  void SetOtherMusicPlaying(bool playing);
  bool OtherMusicPlaying() const { return other_music_; }

  bool HasDevice() const { return device_ != nullptr; }
  Mixer& GetMixer() { return *mixer_; }

 private:
  struct Device;       // miniaudio's device, kept out of this header
  struct MusicStream;  // a miniaudio decoder over a file in memory
  std::unique_ptr<Mixer> mixer_;
  std::unique_ptr<Device> device_;
  bool suspended_ = false;
  bool interrupted_ = false;
  void UpdateDevice();  // starts or stops the device from the two flags
  bool other_music_ = false;
  float music_volume_ = 1.0f;  // Bus::Music's volume as the game set it
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
