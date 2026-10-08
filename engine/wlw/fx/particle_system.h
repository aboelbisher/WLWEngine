#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include <glm/glm.hpp>

#include "wlw/core/color.h"
#include "wlw/rendering/instance_buffer.h"

namespace wlw::fx {

// How one burst spawns its particles. Every "min/max" pair is a random range: the variation is
// what makes an effect look organic instead of mechanical.
struct BurstSettings {
  int count_min = 8;
  int count_max = 12;
  glm::vec3 direction{0.0f, 1.0f, 0.0f};  // main direction particles fly in (normalised for you)
  float spread_degrees = 60.0f;           // half-angle of the cone around `direction` (180 = all)
  float speed_min = 2.0f;
  float speed_max = 5.0f;
  float position_jitter = 0.0f;  // spawn anywhere in a cube of this half-size around the origin
  float lifetime_min = 0.5f;
  float lifetime_max = 0.9f;
  float size_start = 0.25f;  // uniform scale of the particle's mesh, shrinking/growing linearly
  float size_end = 0.0f;     //   to size_end over its life
  float spin_max = 720.0f;   // degrees per second, random per axis in [-spin_max, spin_max]
  core::Color color{1.0f, 1.0f, 1.0f, 1.0f};
  float brightness_jitter = 0.15f;  // each particle's colour is scaled by 1 +- this
  float delay = 0.0f;               // seconds before the particles appear (for staggered effects)
};

// A CPU particle simulation that outputs GPU instances: the particles of all bursts are drawn as
// copies of one mesh in a single instanced draw call (Node::SetInstances).
//
// Storage is one contiguous array with a fixed capacity. A dead particle is replaced by the last
// one ("swap-remove"): O(1), no gaps, no allocation per particle. Bursts beyond the capacity are
// cut short rather than growing memory mid-game.
class ParticleSystem {
 public:
  struct Particle {
    glm::vec3 position{0.0f};
    glm::vec3 velocity{0.0f};
    glm::vec3 rotation{0.0f};  // degrees around x, y, z
    glm::vec3 spin{0.0f};      // degrees per second
    float size_start = 1.0f;
    float size_end = 0.0f;
    core::Color color{1.0f, 1.0f, 1.0f, 1.0f};
    float age = 0.0f;  // seconds; negative while still delayed (not shown, not moving)
    float lifetime = 1.0f;

    float Size() const;  // at the current age
  };

  explicit ParticleSystem(size_t capacity = 1024, uint32_t seed = 0x2545F491u);

  // Spawns a burst at `origin`. Returns how many particles were actually created.
  int Burst(const BurstSettings& settings, const glm::vec3& origin);

  // Advances every particle by dt seconds and removes the ones that reached their lifetime.
  void Update(float dt);

  // One instance per visible (alive, not delayed) particle, for Node::SetInstances.
  void BuildInstances(std::vector<rendering::InstanceData>& out) const;

  void Clear() { particles_.clear(); }
  size_t Count() const { return particles_.size(); }
  size_t Capacity() const { return capacity_; }
  const std::vector<Particle>& Particles() const { return particles_; }

  // Applied to every particle. Drag is "fraction of speed lost per second"-like: velocity is
  // multiplied by exp(-drag * dt), which gives the same motion at any frame rate.
  glm::vec3 gravity{0.0f, -9.8f, 0.0f};
  float drag = 0.5f;

 private:
  float Random01();
  float RandomRange(float min, float max) { return min + (max - min) * Random01(); }
  glm::vec3 RandomDirectionInCone(const glm::vec3& axis, float half_angle_degrees);

  std::vector<Particle> particles_;
  size_t capacity_;
  uint32_t rng_state_;
};

}  // namespace wlw::fx
