// Tests for wlw::audio: decoding and the Mixer. No audio device needed: the mixer is driven
// directly, the way the device's callback would.
#include <cmath>
#include <algorithm>
#include <cstring>
#include <filesystem>
#include <vector>

#include "test_helpers.h"
#include "wlw/audio/audio.h"
#include "wlw/audio/mixer.h"
#include "wlw/platform/platform.h"

TEST_SUITE_BEGIN("audio");

using namespace wlw::audio;

namespace {

constexpr uint32_t kRate = 48000;

// A minimal 16-bit PCM mono WAV file in memory.
std::vector<uint8_t> MakeWav(uint32_t sample_rate, const std::vector<int16_t>& samples) {
  const auto frames = static_cast<uint32_t>(samples.size());
  std::vector<uint8_t> wav;
  auto put = [&wav](const void* data, size_t size) {
    const auto* bytes = static_cast<const uint8_t*>(data);
    wav.insert(wav.end(), bytes, bytes + size);
  };
  auto u32 = [&put](uint32_t v) { put(&v, 4); };
  auto u16 = [&put](uint16_t v) { put(&v, 2); };
  put("RIFF", 4);
  u32(36 + frames * 2);
  put("WAVEfmt ", 8);
  u32(16);
  u16(1);  // PCM
  u16(1);  // mono
  u32(sample_rate);
  u32(sample_rate * 2);
  u16(2);
  u16(16);
  put("data", 4);
  u32(frames * 2);
  for (int16_t s : samples) u16(static_cast<uint16_t>(s));
  return wav;
}

// ...of `frames` samples of a 440 Hz sine.
std::vector<uint8_t> MakeWav(uint32_t sample_rate, uint32_t frames) {
  std::vector<int16_t> samples(frames);
  for (uint32_t i = 0; i < frames; ++i)
    samples[i] = static_cast<int16_t>(12000.0 * std::sin(2.0 * 3.14159265 * 440.0 * i / sample_rate));
  return MakeWav(sample_rate, samples);
}

// A sound with a constant value, `frames` long: easy to check in the mix.
SoundData Constant(float value, uint32_t frames) {
  SoundData data;
  data.samples.assign(frames * 2, value);
  data.frames = frames;
  return data;
}

std::vector<float> Mix(Mixer& mixer, uint32_t frames) {
  std::vector<float> out(frames * 2, -99.0f);  // garbage: Mix must overwrite everything
  mixer.Mix(out.data(), frames);
  return out;
}

// A stream that plays a constant level, forever or for `length` frames.
class ConstantStream : public StreamSource {
 public:
  ConstantStream(float level, uint32_t length = 0) : level_(level), left_(length), endless_(length == 0) {}
  uint32_t Read(float* stereo, uint32_t frames) override {
    const uint32_t n = endless_ ? frames : std::min(frames, left_);
    for (uint32_t i = 0; i < n * 2; ++i) stereo[i] = level_;
    if (!endless_) left_ -= n;
    return n;
  }

 private:
  float level_;
  uint32_t left_;
  bool endless_;
};

TEST_CASE("streams fade and crossfade") {
  Mixer mixer(1000);  // 1000 frames per second: a 0.1 s fade is 100 frames
  ConstantStream a(0.5f), b(-0.5f);
  std::vector<float> out(2 * 400);
  mixer.PlayStream(&a, 0.1f);
  mixer.Mix(out.data(), 50);
  CHECK((out[0] > 0.0f && out[0] < 0.02f));         // fading in from silence
  CHECK(Near(out[49 * 2], 0.5f * 0.7071f, 0.01f));  // halfway: sin(pi/4) = equal power, not 0.5
  mixer.Mix(out.data(), 100);
  CHECK((Near(out[99 * 2], 0.5f) && Near(out[99 * 2 + 1], 0.5f)));  // fully in
  mixer.SetBusVolume(Bus::Music, 0.5f);
  mixer.Mix(out.data(), 10);
  CHECK(Near(out[0], 0.25f));  // the music bus volume applies
  mixer.SetBusVolume(Bus::Music, 1.0f);

  // Crossfade: a fades out while b fades in; halfway they cancel out (0.35 - 0.35).
  mixer.PlayStream(&b, 0.1f);
  mixer.Mix(out.data(), 200);
  CHECK(Near(out[49 * 2], 0.0f, 0.03f));
  CHECK(Near(out[199 * 2], -0.5f));  // only b left
  mixer.PlayStream(&b, 0.1f);        // already playing: nothing changes
  mixer.Mix(out.data(), 10);
  CHECK(Near(out[9 * 2], -0.5f));

  // Stop: fades to silence, then the slot is free.
  mixer.PlayStream(nullptr, 0.1f);
  mixer.Mix(out.data(), 200);
  CHECK((Near(out[199 * 2], 0.0f) && Near(out[50 * 2], -0.35f, 0.03f)));

  // A stream that ends stops by itself (its remaining frames are silence).
  ConstantStream short_one(0.5f, 30);
  mixer.PlayStream(&short_one, 0.001f);
  mixer.Mix(out.data(), 60);
  CHECK((Near(out[20 * 2], 0.5f) && Near(out[45 * 2], 0.0f)));
  // Muting silences music too.
  mixer.PlayStream(&a, 0.001f);
  mixer.SetMuted(true);
  mixer.Mix(out.data(), 20);
  CHECK(Near(out[10 * 2], 0.0f));
}

TEST_CASE("stream fades: equal power, volume, no cuts") {
  Mixer mixer(1000);
  ConstantStream a(1.0f), b(1.0f), c(1.0f);
  std::vector<float> out(2 * 400);

  // The fade-in curve is sin(t pi/2): 0.383 at a quarter, 0.707 at half, 0.924 at three quarters.
  mixer.PlayStream(&a, 0.1f, 0.5f);  // a trim of 0.5
  mixer.Mix(out.data(), 100);
  CHECK(Near(out[24 * 2], 0.5f * 0.3827f, 0.01f));
  CHECK(Near(out[49 * 2], 0.5f * 0.7071f, 0.01f));
  CHECK(Near(out[74 * 2], 0.5f * 0.9239f, 0.01f));
  CHECK(Near(out[99 * 2], 0.5f));
  mixer.PlayStream(&a, 0.1f, 0.8f);  // the same stream again: only its volume changes
  mixer.Mix(out.data(), 10);
  CHECK(Near(out[0], 0.8f));

  // Two equally loud streams crossfading keep the power: gain_a^2 + gain_b^2 = 0.8^2 all through.
  mixer.PlayStream(&b, 0.1f, 0.8f);
  for (int i = 0; i < 10; ++i) {
    mixer.Mix(out.data(), 10);
    // Both read 1.0, so the sum is gain_a + gain_b = 0.8 (cos + sin) >= 0.8, peaking at 0.8 x 1.414.
    CHECK((out[9 * 2] >= 0.79f && out[9 * 2] <= 0.8f * 1.415f));
  }
  CHECK(Near(out[9 * 2], 0.8f));  // only b

  // Stop, then play the same stream inside the fade-out: it turns round where it is, no cut.
  mixer.PlayStream(nullptr, 0.1f);
  mixer.Mix(out.data(), 50);
  const float low = out[49 * 2];
  CHECK((low > 0.4f && low < 0.7f));
  mixer.PlayStream(&b, 0.1f, 0.8f);
  mixer.Mix(out.data(), 100);
  CHECK(std::fabs(out[0] - low) < 0.03f);  // carries on from the level it had
  CHECK(Near(out[99 * 2], 0.8f));

  // Three quick switches: b, a, c all fading at once; nothing drops out with a click.
  mixer.PlayStream(&a, 0.1f);
  mixer.Mix(out.data(), 20);
  mixer.PlayStream(&c, 0.1f);
  float previous = out[19 * 2];
  mixer.Mix(out.data(), 200);
  for (int i = 0; i < 200; ++i) {
    CHECK(std::fabs(out[i * 2] - previous) < 0.05f);  // smooth: no step from a cut stream
    previous = out[i * 2];
  }
  CHECK(Near(out[199 * 2], 1.0f));  // only c, at full volume
}

TEST_CASE("streams in use") {
  Mixer mixer(1000);
  ConstantStream a(0.5f), b(0.5f);
  CHECK(!a.InUse());
  mixer.PlayStream(&a, 0.05f);
  CHECK(a.InUse());  // queued: the audio thread hasn't seen it yet, but may
  Mix(mixer, 100);
  CHECK(a.InUse());  // in a slot
  mixer.PlayStream(&b, 0.05f);
  Mix(mixer, 20);
  CHECK(a.InUse());  // fading out
  Mix(mixer, 60);
  CHECK((!a.InUse() && b.InUse()));  // faded out: free to destroy

  // More fades at once than slots: the quietest is dropped, and is no longer in use.
  ConstantStream s[6] = {ConstantStream(0.1f), ConstantStream(0.1f), ConstantStream(0.1f),
                         ConstantStream(0.1f), ConstantStream(0.1f), ConstantStream(0.1f)};
  for (auto& stream : s) {
    mixer.PlayStream(&stream, 1.0f);    // the one before fades out over a second...
    mixer.PlayStream(&stream, 0.001f);  // ...while this one comes in at once (already playing: a new fade)
    Mix(mixer, 5);
  }
  int in_use = b.InUse() ? 1 : 0;
  for (auto& stream : s) in_use += stream.InUse() ? 1 : 0;
  CHECK(in_use == static_cast<int>(Mixer::kStreamSlots));
}

}  // namespace

TEST_CASE("music: intro into loop, restarts") {
  namespace fs = std::filesystem;
  const fs::path folder = fs::temp_directory_path() / "wlw_audio_tests";
  fs::create_directories(folder);
  const std::string intro = (folder / "intro.wav").string(), loop = (folder / "loop.wav").string();
  // The intro: 3000 frames at a constant level. The loop: a 1000-frame ramp, so a missing or doubled
  // frame at a seam shows up as a wrong value.
  CHECK(wlw::platform::WriteFileAtomic(intro, MakeWav(kRate, std::vector<int16_t>(3000, 8192))));
  std::vector<int16_t> ramp(1000);
  for (int i = 0; i < 1000; ++i) ramp[i] = static_cast<int16_t>(i * 16);
  CHECK(wlw::platform::WriteFileAtomic(loop, MakeWav(kRate, ramp)));

  Audio audio(AudioConfig{.enabled = false});  // no device: the test plays the audio thread
  Mixer& mixer = audio.GetMixer();
  CHECK(mixer.SampleRate() == kRate);  // no resampling, so frames line up exactly
  CHECK(audio.PlayMusic(loop, {.fade = 0.001f, .intro = intro}));
  std::vector<float> out = Mix(mixer, 6000);
  CHECK(Near(out[2999 * 2], 0.25f));
  bool exact = true;
  for (int k = 0; k < 3000; ++k) {  // the seam into the loop, and two of the loop's own seams
    exact = exact && Near(out[(3000 + k) * 2], static_cast<float>((k % 1000) * 16) / 32768.0f, 1e-4f);
  }
  CHECK(exact);

  // Playing it again (no restart) keeps the playthrough; a restart makes a new one, and the old
  // one is freed once it has faded out.
  CHECK(audio.PlayMusic(loop));
  CHECK(audio.MusicStreamCount() == 1);
  CHECK(audio.PlayMusic(loop, {.fade = 0.001f, .restart = true}));
  CHECK(audio.MusicStreamCount() == 2);  // the old one is still fading out
  Mix(mixer, 200);
  CHECK(audio.PlayMusic(loop, {.fade = 0.001f}));  // the newest: nothing changes, the faded one goes
  CHECK(audio.MusicStreamCount() == 1);
  CHECK(!audio.PlayMusic((folder / "missing.ogg").string()));
  fs::remove_all(folder);
}

TEST_CASE("decode resamples to stereo") {
  const std::vector<uint8_t> wav = MakeWav(22050, 2205);  // 0.1 s, mono, 22.05 kHz
  const auto data = Decode(wav.data(), wav.size(), kRate);
  CHECK(data.has_value());
  if (!data) return;
  CHECK(data->samples.size() == data->frames * 2);      // stereo
  CHECK((data->frames > 4700 && data->frames < 4900));  // 0.1 s at 48 kHz
  float peak = 0.0f;
  for (float s : data->samples) peak = std::max(peak, std::fabs(s));
  CHECK((peak > 0.3f && peak < 0.4f));                // 12000 / 32768
  CHECK(!Decode(wav.data(), 10, kRate).has_value());  // truncated: rejected, no crash
}

TEST_CASE("mixes volume and pan") {
  Mixer mixer(kRate);
  const auto sound = mixer.AddSound(Constant(0.5f, 1000));
  auto out = Mix(mixer, 10);
  CHECK((out[0] == 0.0f && out[19] == 0.0f));  // silence before anything plays

  mixer.Play(sound, {.volume = 0.5f});
  out = Mix(mixer, 10);
  CHECK((Near(out[0], 0.25f) && Near(out[1], 0.25f)));
  CHECK(mixer.ActiveVoices() == 1);

  mixer.StopAll();
  mixer.Play(sound, {.pan = 1.0f});  // hard right
  out = Mix(mixer, 10);
  CHECK((Near(out[0], 0.0f) && Near(out[1], 0.5f)));

  mixer.SetMuted(true);
  out = Mix(mixer, 10);
  CHECK(out[1] == 0.0f);
  mixer.SetMuted(false);
  mixer.SetBusVolume(Bus::Effects, 0.5f);
  out = Mix(mixer, 10);
  CHECK(Near(out[1], 0.25f));
}

TEST_CASE("pitch and end") {
  Mixer mixer(kRate);
  const auto sound = mixer.AddSound(Constant(0.5f, 100));
  mixer.Play(sound, {.pitch = 2.0f});
  Mix(mixer, 40);
  CHECK(mixer.ActiveVoices() == 1);  // 80 of 100 frames used
  Mix(mixer, 40);
  CHECK(mixer.ActiveVoices() == 0);  // twice the speed: done after ~50 output frames

  mixer.Play(sound, {.loop = true});
  Mix(mixer, 1000);
  CHECK(mixer.ActiveVoices() == 1);  // a looping sound keeps going
}

TEST_CASE("voice limits") {
  Mixer mixer(kRate, /*max_voices=*/6);
  mixer.max_instances_per_sound = 3;
  const auto pop = mixer.AddSound(Constant(0.1f, 10000));
  const auto other = mixer.AddSound(Constant(0.1f, 10000));
  for (int i = 0; i < 5; ++i) mixer.Play(pop);
  Mix(mixer, 1);
  CHECK(mixer.ActiveVoices() == 3);  // copies of one sound are capped: the oldest is replaced

  for (int i = 0; i < 5; ++i) mixer.Play(other);
  Mix(mixer, 1);
  CHECK(mixer.ActiveVoices() == 6);  // 3 + 3: all voices busy

  mixer.max_instances_per_sound = 100;
  for (int i = 0; i < 4; ++i) mixer.Play(other);
  Mix(mixer, 1);
  CHECK(mixer.ActiveVoices() == 6);  // full: new sounds steal the oldest voices, never more
}

TEST_CASE("many plays in one frame") {
  Mixer mixer(kRate);
  const auto sound = mixer.AddSound(Constant(0.9f, 100));
  for (int i = 0; i < 1000; ++i) mixer.Play(sound);  // more than the command queue holds
  const auto out = Mix(mixer, 50);
  for (float s : out) CHECK((s <= 1.0f && s >= -1.0f));  // clipped, never wraps around
  CHECK(mixer.ActiveVoices() <= 4);
}

TEST_SUITE_END();
