#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

#include <glm/glm.hpp>

#include "wlw/core/color.h"
#include "wlw/core/deprecated.h"
#include "wlw/core/random.h"
#include "wlw/rendering/instance_buffer.h"
#include "wlw/scene/geometry_instance.h"

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
  Color color{1.0f, 1.0f, 1.0f, 1.0f};
  float brightness_jitter = 0.15f;  // each particle's colour is scaled by 1 +- this
  float delay = 0.0f;               // seconds before the particles appear (for staggered effects)
};

// A CPU particle simulation that outputs GPU instances: the particles of all bursts are drawn as
// copies of one mesh in a single instanced draw call (WriteTo: MultiMeshInstance3D::WriteInstances).
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
    Color color{1.0f, 1.0f, 1.0f, 1.0f};
    float age = 0.0f;  // seconds; negative while still delayed (not shown, not moving)
    float lifetime = 1.0f;

    float Size() const;  // at the current age
  };

  explicit ParticleSystem(size_t capacity = 1024, uint32_t seed = 0x2545F491u);

  // Spawns one particle exactly as given (bursts are random): a block flying off the board, a piece
  // of debris that must start where something was. A negative `age` delays it, like a burst's
  // delay. Returns false (and adds nothing) when the system is full.
  bool Emit(const Particle& particle);
  // Spawns a burst at `origin`. Returns how many particles were actually created.
  int Burst(const BurstSettings& settings, const glm::vec3& origin);

  // Advances every particle by dt seconds and removes the ones that reached their lifetime.
  void Update(float dt);

  // Makes `node` draw one copy of its model per visible particle (alive, not delayed, size > 0):
  // node.WriteInstances, filled in place (52 bytes each). Call once per frame after Update. The
  // first call reserves the system's capacity in the node, so later ones never allocate.
  void WriteTo(scene::MultiMeshInstance3D& node) const;

#if !WLW_NO_DEPRECATED
  WLW_DISABLE_DEPRECATION_WARNINGS_BEGIN
  WLW_DEPRECATED(0.2, "Use WriteTo(node)")
  void BuildInstances(std::vector<rendering::InstanceData>& out) const;
  WLW_DISABLE_DEPRECATION_WARNINGS_END
#endif

  void Clear() { particles_.clear(); }
  size_t Count() const { return particles_.size(); }
  size_t Capacity() const { return capacity_; }
  const std::vector<Particle>& Particles() const { return particles_; }

  // Applied to every particle. Drag is "fraction of speed lost per second"-like: velocity is
  // multiplied by exp(-drag * dt), which gives the same motion at any frame rate.
  glm::vec3 gravity{0.0f, -9.8f, 0.0f};
  // An optional floor: a horizontal rectangle (x from min.x to max.x, z from min.y to max.y) at
  // `height`. Particles falling onto it bounce (losing speed) and slide (losing more), and come to
  // rest; past its edges they fall on by. Particles count as cubes of their current size.
  struct Floor {
    float height = 0.0f;
    glm::vec2 min{-1e9f, -1e9f};
    glm::vec2 max{1e9f, 1e9f};
    float bounce = 0.35f;   // share of the downward speed kept, upwards (0 = dead stop, 1 = no loss)
    float friction = 0.5f;  // share of the sideways speed and spin lost at each bounce
  };
  std::optional<Floor> floor;
  float drag = 0.5f;

 private:
  glm::vec3 RandomDirectionInCone(const glm::vec3& axis, float half_angle_degrees);

  std::vector<Particle> particles_;
  size_t capacity_;
  core::Random rng_;  // visuals: any sequence would do, but one shared generator is simpler
};

}  // namespace wlw::fx
