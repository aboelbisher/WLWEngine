#pragma once

#include <atomic>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "wlw/audio/audio_config.h"
#include "wlw/audio/mixer.h"

namespace wlw::audio {

// How Audio::PlayMusic starts a track.
struct MusicParams {
  float fade = 1.5f;     // seconds: this track fades in while the one before fades out
  float volume = 1.0f;   // this track's level: a trim that evens out tracks mastered louder or quieter
  std::string intro;     // a file played once first, straight into the track's loop (no gap): a jingle
  bool restart = false;  // from the top, even if the track was playing or paused (else it resumes)
};

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
  // ~40 MB as floats). Loops forever, on Bus::Music, crossfading (equal-power) from the previous track.
  //   Audio().PlayMusic("music/stage_2.ogg", {.fade = 0.6f, .volume = 0.9f});
  //   Audio().PlayMusic("music/win_loop.ogg", {.fade = 0.2f, .intro = "music/win_jingle.ogg"});
  // The track already playing only takes the new volume. A track played before and faded away resumes
  // where it was, unless `restart` (or an `intro`) asks for a new playthrough from the top; the old one
  // finishes its fade and is freed. False if a file can't be opened.
  bool PlayMusic(const std::string& path, const MusicParams& params = {});
  void StopMusic(float fade_seconds = 1.0f) { mixer_->PlayStream(nullptr, fade_seconds); }
  size_t MusicStreamCount();  // playthroughs alive (playing, fading, kept to resume): tests, debug panels

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
  struct MusicStream;  // one playthrough: miniaudio decoders over files in memory
  std::unique_ptr<Mixer> mixer_;
  std::unique_ptr<Device> device_;
  bool suspended_ = false;
  bool interrupted_ = false;
  void UpdateDevice();  // starts or stops the device from the two flags
  bool other_music_ = false;
  float music_volume_ = 1.0f;  // Bus::Music's volume as the game set it
  // Every music file read so far, by path (a restart decodes the same bytes again).
  using Bytes = std::shared_ptr<const std::vector<uint8_t>>;
  std::map<std::string, Bytes> music_files_;  // guarded by music_mutex_
  Bytes MusicFile(const std::string& path);   // read once; null if missing
  // Every playthrough alive. A retired one (replaced by a restart) is destroyed once the mixer no
  // longer holds it (StreamSource::InUse); ~Audio stops the device before any of them is destroyed.
  std::vector<std::unique_ptr<MusicStream>> music_;  // guarded by music_mutex_
  void FreeRetiredMusic();                           // with music_mutex_ held
  // The music decode thread (started by the first PlayMusic): keeps every stream's ring topped up.
  void DecodeLoop();
  std::mutex music_mutex_;
  std::thread decode_thread_;
  std::atomic<bool> decoding_{false};
};

}  // namespace wlw::audio
