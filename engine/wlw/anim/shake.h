#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

#include <glm/glm.hpp>

namespace wlw::anim {

// Trauma-based screen shake (the technique Squirrel Eiserloh popularised).
//
// Big moments ADD trauma (0..1); it decays at a constant rate. How much the screen moves is
// trauma squared, so small hits barely move it and big ones really kick, and several hits in a
// row build on each other instead of restarting. The motion comes from smooth noise rather than
// a new random offset every frame: it reads as a shove, not as jitter, at any frame rate.
//
// Like a Tween, the game owns it and samples it:
//   shake.AddTrauma(0.5f);                          // a big clear
//   shake.Update(dt);
//   camera_position += right * shake.Offset().x + up * shake.Offset().y;
class Shake {
 public:
  explicit Shake(uint32_t seed = 1) : seed_(seed) {}

  float max_offset = 0.3f;  // at full trauma, in whatever units the game applies it in
  float frequency = 15.0f;  // how fast it wobbles: noise lattice points per second
  float decay = 1.4f;       // trauma lost per second

  void AddTrauma(float amount) { trauma_ = std::clamp(trauma_ + amount, 0.0f, 1.0f); }
  float Trauma() const { return trauma_; }
  float Amount() const { return trauma_ * trauma_; }  // what the offset is scaled by
  bool Active() const { return trauma_ > 0.0f; }

  void Update(float dt) {
    time_ += dt;
    trauma_ = std::max(0.0f, trauma_ - decay * dt);
  }
  void Reset() { trauma_ = 0.0f; }

  // The current offset (x, y), each in [-max_offset, max_offset]. Zero without trauma.
  glm::vec2 Offset() const {
    const float scale = Amount() * max_offset;
    if (scale == 0.0f) return glm::vec2(0.0f);
    const float t = time_ * frequency;
    return {scale * Noise(t, 0), scale * Noise(t, 1)};
  }

 private:
  // 1D value noise in [-1, 1]: a random value at every whole t, eased between them (smoothstep).
  float Noise(float t, uint32_t channel) const {
    const float cell = std::floor(t);
    const float f = t - cell;
    const float ease = f * f * (3.0f - 2.0f * f);
    const int32_t i = static_cast<int32_t>(cell);
    return Lattice(i, channel) + (Lattice(i + 1, channel) - Lattice(i, channel)) * ease;
  }
  float Lattice(int32_t i, uint32_t channel) const {
    // A small integer hash (no state): the same i always gives the same value.
    uint32_t h = static_cast<uint32_t>(i) * 0x9E3779B1u ^ (seed_ + channel * 0x85EBCA77u);
    h ^= h >> 15;
    h *= 0x2C1B3C6Du;
    h ^= h >> 12;
    h *= 0x297A2D39u;
    h ^= h >> 15;
    return static_cast<float>(h & 0xFFFFFFu) / static_cast<float>(0xFFFFFFu) * 2.0f - 1.0f;
  }

  uint32_t seed_;
  float trauma_ = 0.0f;
  float time_ = 0.0f;
};

}  // namespace wlw::anim
