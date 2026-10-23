// UI text and audio mixing: Canvas::Text at 1k glyphs a frame (a busy HUD), and the Mixer with
// all 24 voices playing (one audio-callback block of 512 frames).
#include <cstdio>
#include <vector>

#include "bench.h"
#include "wlw/audio/mixer.h"
#include "wlw/core/random.h"
#include "wlw/platform/platform.h"
#include "wlw/ui/canvas.h"
#include "wlw/ui/font.h"

using namespace wlw;

namespace {

// 20 lines of 50 characters = 1000 glyphs, half of them outlined (the text shader's other path).
void TextBenchmarks(bench::Context& ctx) {
  const std::string path = ctx.DataDir() + "/LilitaOne-Regular.ttf";
  std::optional<std::vector<uint8_t>> ttf = platform::ReadLocalFile(path);
  std::shared_ptr<ui::Font> font = ttf ? ui::Font::FromMemory(std::move(*ttf)) : nullptr;
  if (!font) {
    std::fprintf(stderr, "ui benchmarks skipped: no font at %s (pass --data <dir>)\n", path.c_str());
    return;
  }
  const std::string line = "Level 12 - Score 004250 - Best 019870 - Combo x3!!";  // 50 characters
  ui::Canvas canvas;
  ui::TextStyle outlined;
  outlined.outline = 0.05f;
  ctx.Run("ui/text 1k glyphs", [&] {
    canvas.Internal().BeginFrame({1080.0f, 1920.0f}, {1080.0f, 1920.0f});
    for (int i = 0; i < 20; ++i) {
      canvas.Text(*font, line, {20.0f, 40.0f + 60.0f * static_cast<float>(i)}, 36.0f, ui::Align::Left,
                  i % 2 ? outlined : ui::TextStyle{});
    }
    bench::Context::Keep(canvas.Size());
  });
}

// 24 different looping sounds (the per-sound cap is 4 copies), at mixed pitches and pans.
void MixerBenchmarks(bench::Context& ctx) {
  constexpr uint32_t kRate = 48000;
  constexpr uint32_t kFrames = 512;
  audio::Mixer mixer(kRate, 24);
  core::Random random(7u);
  for (int i = 0; i < 24; ++i) {
    audio::SoundData sound;
    sound.frames = kRate;  // 1 s of noise
    sound.samples.resize(static_cast<size_t>(sound.frames) * 2);
    for (float& sample : sound.samples) sample = random.Range(-0.5f, 0.5f);
    const audio::Mixer::SoundId id = mixer.AddSound(std::move(sound));
    mixer.Play(id, {.volume = 0.5f,
                    .pitch = i % 3 == 0 ? 1.0f : 0.75f + 0.05f * static_cast<float>(i % 7),
                    .pan = random.Range(-1.0f, 1.0f),
                    .loop = true});
  }
  std::vector<float> out(kFrames * 2);
  mixer.Mix(out.data(), kFrames);  // takes the play commands
  ctx.Run("audio/mix 24 voices", [&] {
    mixer.Mix(out.data(), kFrames);
    bench::Context::Keep(out[0]);
  });
  if (mixer.ActiveVoices() != 24) std::fprintf(stderr, "audio/mix: %d voices playing, not 24\n", mixer.ActiveVoices());
}

}  // namespace

WLW_BENCHMARK("ui") { TextBenchmarks(ctx); }

WLW_BENCHMARK("audio") { MixerBenchmarks(ctx); }
