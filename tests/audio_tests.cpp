// Tests for wlw::audio: decoding and the Mixer. No audio device needed: the mixer is driven
// directly, the way the device's callback would.
#include <cmath>
#include <algorithm>
#include <cstring>
#include <filesystem>
#include <vector>

#include "test_helpers.h"
#include "wlw/audio/audio.h"
#include "wlw/audio/filter.h"
#include "wlw/audio/mixer.h"
#include "wlw/core/random.h"
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

// ---- Live mix: Filter, bus gain, ducking ----

namespace {

// RMS of the left channel after `skip` frames: how loud a steady signal comes out.
float Rms(const std::vector<float>& stereo, size_t skip) {
  double sum = 0.0;
  size_t n = 0;
  for (size_t i = skip; i < stereo.size() / 2; ++i, ++n) sum += double(stereo[i * 2]) * stereo[i * 2];
  return n ? static_cast<float>(std::sqrt(sum / static_cast<double>(n))) : 0.0f;
}

std::vector<float> Sine(float hz, uint32_t frames, float level = 0.5f, uint32_t rate = kRate) {
  std::vector<float> stereo(frames * 2);
  for (uint32_t i = 0; i < frames; ++i) {
    stereo[i * 2] = stereo[i * 2 + 1] = level * static_cast<float>(std::sin(2.0 * 3.14159265 * hz * i / rate));
  }
  return stereo;
}

// How much `filter` lets through of a sine at `hz` (1 = all, 0.5 = -6 dB).
float Response(Filter filter, float hz) {
  std::vector<float> s = Sine(hz, 9600);
  filter.Process(s.data(), 9600);
  return Rms(s, 4800) / (0.5f / std::sqrt(2.0f));
}

// A stream of a sine (for the music bus's filters).
class SineStream : public StreamSource {
 public:
  SineStream(float hz, uint32_t rate) : step_(2.0 * 3.14159265 * hz / rate) {}
  uint32_t Read(float* stereo, uint32_t frames) override {
    for (uint32_t i = 0; i < frames; ++i)
      stereo[i * 2] = stereo[i * 2 + 1] = 0.5f * static_cast<float>(std::sin(phase_ += step_));
    return frames;
  }

 private:
  double step_;
  double phase_ = 0.0;
};

}  // namespace

TEST_CASE("filter: response") {
  const Filter low(Filter::Kind::LowPass, kRate);
  Filter low_1k = low;
  low_1k.SetCutoff(1000.0f, 0.0f);
  CHECK(Near(Response(low_1k, 100.0f), 1.0f, 0.02f));     // well below the cutoff: passes
  CHECK(Near(Response(low_1k, 1000.0f), 0.707f, 0.03f));  // at the cutoff: -3 dB (Q 0.707)
  CHECK(Response(low_1k, 8000.0f) < 0.02f);               // 3 octaves above: 12 dB per octave, < -34 dB
  Filter high(Filter::Kind::HighPass, kRate);
  high.SetCutoff(1000.0f, 0.0f);
  CHECK(Near(Response(high, 8000.0f), 1.0f, 0.02f));
  CHECK(Near(Response(high, 1000.0f), 0.707f, 0.03f));
  CHECK(Response(high, 125.0f) < 0.02f);

  // Open (the default): untouched, bit for bit.
  Filter open(Filter::Kind::LowPass, kRate);
  CHECK(open.Open());
  std::vector<float> s = Sine(3000.0f, 512), copy = s;
  open.Process(s.data(), 512);
  CHECK(s == copy);
}

TEST_CASE("filter: glides in octaves, leaves bypass without a step") {
  Filter filter(Filter::Kind::LowPass, kRate);
  std::vector<float> s = Sine(200.0f, kRate);  // 1 s of a low note: passes at any cutoff here
  filter.Process(s.data(), 4800);              // open: bypassed
  filter.SetCutoff(400.0f, 1.0f);              // from fully open (~20 kHz): 5.6 octaves down in 1 s
  filter.Process(s.data() + 4800 * 2, 4800);
  // Coming out of bypass the integrators start where the signal is: no jump at the seam.
  float worst = 0.0f;
  for (int i = 4790; i < 4900; ++i) worst = std::max(worst, std::fabs(s[(i + 1) * 2] - s[i * 2]));
  CHECK(worst < 0.02f);  // a 200 Hz sine at 0.5 moves up to 0.013 a sample; a cold start would step by up to 0.5

  filter.SetCutoff(400.0f, 0.0f);
  filter.SetCutoff(6400.0f, 1.0f);  // 4 octaves up in 1 s
  std::vector<float> buffer(4800 * 2, 0.0f);
  for (int i = 0; i < 5; ++i) filter.Process(buffer.data(), 4800);
  CHECK(Near(filter.Cutoff(), 1600.0f, 10.0f));  // halfway in time = halfway in octaves (2 of 4), not 3400 Hz
  for (int i = 0; i < 5; ++i) filter.Process(buffer.data(), 4800);
  CHECK(Near(filter.Cutoff(), 6400.0f, 1.0f));
  CHECK(!filter.Open());
  filter.SetCutoff(Filter::kHighestHz, 0.0f);
  CHECK(filter.Open());
}

TEST_CASE("filter: random sweeps stay stable") {
  // The reason for the SVF: cutoffs thrown around every few samples, with resonance, never blow up.
  wlw::core::Random random(11u);
  for (float q : {0.7071f, 4.0f}) {
    Filter filter(Filter::Kind::LowPass, kRate, q);
    std::vector<float> s(64 * 2);
    float peak = 0.0f;
    bool finite = true;
    for (int block = 0; block < 2000; ++block) {
      filter.SetCutoff(random.Range(20.0f, 20000.0f), random.Range(0.0f, 0.002f));
      for (float& x : s) x = random.Range(-0.5f, 0.5f);
      filter.Process(s.data(), 64);
      for (float x : s) {
        finite = finite && std::isfinite(x);
        peak = std::max(peak, std::fabs(x));
      }
    }
    CHECK(finite);
    CHECK(peak < 0.5f * q * 4.0f);  // resonance lifts the cutoff band by about Q, never runs away
  }
}

TEST_CASE("bus live mix: filter, gain, duck") {
  Mixer mixer(kRate);
  SineStream high_note(8000.0f, kRate);
  mixer.PlayStream(&high_note, 0.001f);
  std::vector<float> out = Mix(mixer, 4800);
  CHECK(Near(Rms(out, 480), 0.354f, 0.01f));  // 0.5 / sqrt(2): untouched
  mixer.SetBusLowPass(Bus::Music, 500.0f, 0.0f);
  out = Mix(mixer, 4800);
  CHECK(Rms(out, 480) < 0.354f * 0.01f);  // 4 octaves above the cutoff: -48 dB
  CHECK(Near(mixer.Levels(Bus::Music).low_pass_hz, 500.0f, 1.0f));
  CHECK(Near(mixer.Levels(Bus::Effects).low_pass_hz, Filter::kHighestHz, 1.0f));
  mixer.SetBusLowPass(Bus::Music, Filter::kHighestHz, 0.0f);
  mixer.SetBusHighPass(Bus::Music, 16000.0f, 0.0f);
  out = Mix(mixer, 4800);
  CHECK(Rms(out, 480) < 0.354f * 0.25f);  // the high-pass, an octave above: < -12 dB
  mixer.SetBusHighPass(Bus::Music, Filter::kLowestHz, 0.0f);
  mixer.PlayStream(nullptr, 0.001f);
  Mix(mixer, 100);

  // Gain and ducks, on a constant level (a 1000 Hz mixer: 1 frame = 1 ms).
  Mixer slow(1000);
  ConstantStream level(0.5f);
  slow.PlayStream(&level, 0.001f);
  Mix(slow, 10);
  slow.SetBusGain(Bus::Music, 6.0f, 0.1f);
  out = Mix(slow, 200);
  CHECK(Near(out[49 * 2], 0.5f * 1.413f, 0.01f));  // halfway: +3 dB (gains glide in dB)
  CHECK(Near(out[199 * 2], 0.5f * 1.995f, 0.01f));
  slow.SetBusGain(Bus::Music, 0.0f, 0.0f);

  slow.Duck(Bus::Music, {.depth_db = -6.0f, .attack = 0.1f, .hold = 0.1f, .release = 0.1f});
  out = Mix(slow, 400);
  CHECK(Near(out[49 * 2], 0.5f * 0.708f, 0.01f));   // -3 dB on the way down
  CHECK(Near(out[99 * 2], 0.5f * 0.501f, 0.01f));   // at the bottom...
  CHECK(Near(out[190 * 2], 0.5f * 0.501f, 0.01f));  // ...held
  CHECK(Near(out[250 * 2], 0.5f * 0.708f, 0.02f));  // back up through -3 dB
  CHECK(Near(out[399 * 2], 0.5f));

  // Deepest wins: a lighter duck during a deep one doesn't lift it, it stretches the hold.
  slow.Duck(Bus::Music, {.depth_db = -12.0f, .attack = 0.01f, .hold = 0.1f, .release = 0.1f});
  Mix(slow, 50);
  slow.Duck(Bus::Music, {.depth_db = -3.0f, .attack = 0.01f, .hold = 0.2f, .release = 0.1f});
  out = Mix(slow, 200);
  CHECK(Near(out[0], 0.5f * 0.251f, 0.01f));
  CHECK(Near(out[150 * 2], 0.5f * 0.251f, 0.01f));  // still at -12 dB: the hold ran on
  CHECK(Near(slow.Levels(Bus::Music).duck_db, -12.0f, 0.1f));
  out = Mix(slow, 400);
  CHECK(Near(out[399 * 2], 0.5f));
}

TEST_CASE("bus live mix: other buses untouched") {
  // The music bus filtered, lifted and ducked; an effect on its own bus comes out exactly as before.
  Mixer mixer(kRate);
  const auto sound = mixer.AddSound(Constant(0.25f, 10000));
  mixer.SetBusLowPass(Bus::Music, 300.0f, 0.0f);
  mixer.SetBusHighPass(Bus::Music, 100.0f, 0.0f);
  mixer.SetBusGain(Bus::Music, 6.0f, 0.1f);
  mixer.Duck(Bus::Music);
  mixer.Play(sound, {.volume = 0.5f});
  const std::vector<float> out = Mix(mixer, 2000);
  bool exact = true;
  for (float s : out) exact = exact && s == 0.125f;
  CHECK(exact);
}
TEST_SUITE_END();
