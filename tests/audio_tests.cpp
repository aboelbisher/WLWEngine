// Tests for wlw::audio: decoding and the Mixer. No audio device needed: the mixer is driven
// directly, the way the device's callback would.
#include <cmath>
#include <algorithm>
#include <cstring>
#include <vector>

#include "check.h"
#include "wlw/audio/mixer.h"

using namespace wlw::audio;

namespace {

constexpr uint32_t kRate = 48000;

// A minimal 16-bit PCM mono WAV file in memory: `frames` samples of a 440 Hz sine.
std::vector<uint8_t> MakeWav(uint32_t sample_rate, uint32_t frames) {
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
  for (uint32_t i = 0; i < frames; ++i) u16(static_cast<uint16_t>(static_cast<int16_t>(
      12000.0 * std::sin(2.0 * 3.14159265 * 440.0 * i / sample_rate))));
  return wav;
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

void TestStreamsFadeAndCrossfade() {
  Mixer mixer(1000);  // 1000 frames per second: a 0.1 s fade is 100 frames
  ConstantStream a(0.5f), b(-0.5f);
  std::vector<float> out(2 * 400);
  mixer.PlayStream(&a, 0.1f);
  mixer.Mix(out.data(), 50);
  CHECK(out[0] > 0.0f && out[0] < 0.02f);                   // fading in from silence
  CHECK(Near(out[49 * 2], 0.25f, 0.02f));                   // halfway through the fade
  mixer.Mix(out.data(), 100);
  CHECK(Near(out[99 * 2], 0.5f) && Near(out[99 * 2 + 1], 0.5f));  // fully in
  mixer.SetBusVolume(Bus::Music, 0.5f);
  mixer.Mix(out.data(), 10);
  CHECK(Near(out[0], 0.25f));  // the music bus volume applies
  mixer.SetBusVolume(Bus::Music, 1.0f);

  // Crossfade: a fades out while b fades in; halfway they cancel out (0.25 - 0.25).
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
  CHECK(Near(out[199 * 2], 0.0f) && Near(out[50 * 2], -0.25f, 0.03f));

  // A stream that ends stops by itself (its remaining frames are silence).
  ConstantStream short_one(0.5f, 30);
  mixer.PlayStream(&short_one, 0.001f);
  mixer.Mix(out.data(), 60);
  CHECK(Near(out[20 * 2], 0.5f) && Near(out[45 * 2], 0.0f));
  // Muting silences music too.
  mixer.PlayStream(&a, 0.001f);
  mixer.SetMuted(true);
  mixer.Mix(out.data(), 20);
  CHECK(Near(out[10 * 2], 0.0f));
}

}  // namespace

static void TestDecodeResamplesToStereo() {
  const std::vector<uint8_t> wav = MakeWav(22050, 2205);  // 0.1 s, mono, 22.05 kHz
  const auto data = Decode(wav.data(), wav.size(), kRate);
  CHECK(data.has_value());
  if (!data) return;
  CHECK(data->samples.size() == data->frames * 2);           // stereo
  CHECK(data->frames > 4700 && data->frames < 4900);         // 0.1 s at 48 kHz
  float peak = 0.0f;
  for (float s : data->samples) peak = std::max(peak, std::fabs(s));
  CHECK(peak > 0.3f && peak < 0.4f);  // 12000 / 32768
  CHECK(!Decode(wav.data(), 10, kRate).has_value());  // truncated: rejected, no crash
}

static void TestMixesVolumeAndPan() {
  Mixer mixer(kRate);
  const auto sound = mixer.AddSound(Constant(0.5f, 1000));
  auto out = Mix(mixer, 10);
  CHECK(out[0] == 0.0f && out[19] == 0.0f);  // silence before anything plays

  mixer.Play(sound, {.volume = 0.5f});
  out = Mix(mixer, 10);
  CHECK(Near(out[0], 0.25f) && Near(out[1], 0.25f));
  CHECK(mixer.ActiveVoices() == 1);

  mixer.StopAll();
  mixer.Play(sound, {.pan = 1.0f});  // hard right
  out = Mix(mixer, 10);
  CHECK(Near(out[0], 0.0f) && Near(out[1], 0.5f));

  mixer.SetMuted(true);
  out = Mix(mixer, 10);
  CHECK(out[1] == 0.0f);
  mixer.SetMuted(false);
  mixer.SetBusVolume(Bus::Effects, 0.5f);
  out = Mix(mixer, 10);
  CHECK(Near(out[1], 0.25f));
}

static void TestPitchAndEnd() {
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

static void TestVoiceLimits() {
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

static void TestManyPlaysInOneFrame() {
  Mixer mixer(kRate);
  const auto sound = mixer.AddSound(Constant(0.9f, 100));
  for (int i = 0; i < 1000; ++i) mixer.Play(sound);  // more than the command queue holds
  const auto out = Mix(mixer, 50);
  for (float s : out) CHECK(s <= 1.0f && s >= -1.0f);  // clipped, never wraps around
  CHECK(mixer.ActiveVoices() <= 4);
}

int main() {
  TestDecodeResamplesToStereo();
  TestMixesVolumeAndPan();
  TestPitchAndEnd();
  TestVoiceLimits();
  TestManyPlaysInOneFrame();
  TestStreamsFadeAndCrossfade();
  return TestResult("audio");
}
