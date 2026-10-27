#include "wlw/fx/shockwaves.h"

#include <algorithm>

#include "wlw/anim/ease.h"

namespace wlw::fx {

namespace {

constexpr float kFadeIn = 0.1f;  // of the life

// How far through its life a wave is: < 0 while delayed, >= 1 once over.
float Progress(const ShockwaveSettings& settings, float age) {
  if (settings.duration <= 0.0f) return 1.0f;
  return (age - settings.delay) / settings.duration;
}

}  // namespace

rendering::Ripple ShockwaveAt(glm::vec2 center, const ShockwaveSettings& settings, float age) {
  const float t = Progress(settings, age);
  if (t < 0.0f || t >= 1.0f) return {.center = center, .strength = 0.0f};
  const float fade_in = anim::ease::OutQuad(std::min(t / kFadeIn, 1.0f));
  const float fade_out = (1.0f - t) * (1.0f - t);
  return {.center = center,
          .radius = settings.start_radius + (settings.end_radius - settings.start_radius) * anim::ease::OutCubic(t),
          .width = settings.width + (settings.end_width - settings.width) * t,
          .strength = settings.strength * fade_in * fade_out,
          .chroma = settings.chroma};
}

void Shockwaves::Add(glm::vec2 center, const ShockwaveSettings& settings) {
  // A free slot, else the wave nearest its end.
  Wave* slot = &waves_[0];
  for (Wave& wave : waves_) {
    if (!wave.live) {
      slot = &wave;
      break;
    }
    if (Progress(wave.settings, wave.age) > Progress(slot->settings, slot->age)) slot = &wave;
  }
  *slot = {.center = center, .settings = settings, .age = 0.0f, .live = true};
}

void Shockwaves::Update(float dt) {
  for (Wave& wave : waves_) {
    if (!wave.live) continue;
    wave.age += dt;
    if (Progress(wave.settings, wave.age) >= 1.0f) wave.live = false;
  }
}

void Shockwaves::WriteTo(rendering::PostSettings& settings) const {
  for (size_t i = 0; i < waves_.size(); ++i) {
    const Wave& wave = waves_[i];
    settings.ripples[i] = wave.live ? ShockwaveAt(wave.center, wave.settings, wave.age) : rendering::Ripple{};
  }
}

int Shockwaves::Count() const {
  return static_cast<int>(std::count_if(waves_.begin(), waves_.end(), [](const Wave& wave) { return wave.live; }));
}

}  // namespace wlw::fx
