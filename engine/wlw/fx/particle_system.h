#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

#include <glm/glm.hpp>

#include "wlw/anim/curve.h"
#include "wlw/anim/gradient.h"
#include "wlw/core/color.h"
#include "wlw/core/math.h"
#include "wlw/core/random.h"
#include "wlw/rendering/instance_buffer.h"
#include "wlw/scene/billboard_batch.h"
#include "wlw/scene/geometry_instance.h"

namespace wlw::fx {

// How one burst spawns its particles. Every "min/max" pair is a random range: the variation is
// what makes an effect look organic instead of mechanical.
struct BurstSettings {
  int count_min = 8;
  int count_max = 12;
  glm::vec3 direction = kUp;  // main direction particles fly in (normalised for you)
  float spread = kPi / 3.0f;  // half-angle of the cone around `direction`, radians (kPi = every way)
  float speed_min = 2.0f;
  float speed_max = 5.0f;
  float position_jitter = 0.0f;  // spawn anywhere in a cube of this half-size around the origin
  float lifetime_min = 0.5f;
  float lifetime_max = 0.9f;
  float size_start = 0.25f;     // uniform scale of the particle's mesh, shrinking/growing linearly
  float size_end = 0.0f;        //   to size_end over its life
  float max_spin = 4.0f * kPi;  // radians per second, random per axis in [-max_spin, max_spin]
  Color color{1.0f, 1.0f, 1.0f, 1.0f};
  float brightness_jitter = 0.15f;  // each particle's colour is scaled by 1 +- this
  float intensity = 1.0f;           // HDR multiplier of the colour on billboards (past 1 feeds bloom)
  float delay = 0.0f;               // seconds before the particles appear (for staggered effects)
};

// A CPU particle simulation that outputs GPU instances, in one instanced draw call for all its particles: copies
// of one mesh (WriteTo(MultiMeshInstance3D&): tumbling debris) or camera-facing quads (WriteTo(BillboardBatch&):
// sparks, streaks, flares). Like Unity's Particle System / Godot's CPUParticles, the parts are apart: the
// simulation here (spawn, move, age), the look over a particle's life as data (size_over_life, color_over_life,
// intensity_over_life, stretch: system-wide, like Unity's modules), and the renderer node it writes into.
//
// Storage is one contiguous array with a fixed capacity. A dead particle is replaced by the last
// one ("swap-remove"): O(1), no gaps, no allocation per particle. Bursts beyond the capacity are
// cut short rather than growing memory mid-game.
class ParticleSystem {
 public:
  struct Particle {
    glm::vec3 position{0.0f};
    glm::vec3 velocity{0.0f};
    glm::vec3 angles{0.0f};            // radians about x, y, z (the copy is turned by Rx * Ry * Rz; a billboard by z)
    glm::vec3 angular_velocity{0.0f};  // radians per second about x, y, z
    float size_start = 1.0f;
    float size_end = 0.0f;
    Color color{1.0f, 1.0f, 1.0f, 1.0f};
    float intensity = 1.0f;  // multiplies the colour on billboards (HDR); meshes' RGBA8 tints ignore it
    float age = 0.0f;        // seconds; negative while still delayed (not shown, not moving)
    float lifetime = 1.0f;

    float Size() const;          // at the current age, from size_start / size_end (before size_over_life)
    float LifeFraction() const;  // age / lifetime in [0, 1]
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
  // Makes `batch` draw one quad per visible particle: centre, size (x size_over_life), stretch (velocity x
  // stretch.velocity_scale, at most stretch.max_length long), rotation (angles.z), colour (x color_over_life) and
  // intensity (x intensity_over_life); 40 bytes each, written in place, and the batch's bounds fitted to them. Call
  // once per frame after Update.
  void WriteTo(scene::BillboardBatch& batch) const;

  // The particle's size and colour now, with the over-life settings applied (what WriteTo draws).
  float SizeOf(const Particle& p) const;
  Color ColorOf(const Particle& p) const;
  float IntensityOf(const Particle& p) const;

  void Clear() { particles_.clear(); }
  size_t Count() const { return particles_.size(); }
  size_t Capacity() const { return capacity_; }
  const std::vector<Particle>& Particles() const { return particles_; }

  // Applied to every particle. Drag is "fraction of speed lost per second"-like: velocity is
  // multiplied by exp(-drag * dt), which gives the same motion at any frame rate.
  glm::vec3 gravity{0.0f, 0.0f, -9.8f};
  // An optional floor: a horizontal rectangle (x from min.x to max.x, y from min.y to max.y) at
  // `height` (z). Particles falling onto it bounce (losing speed) and slide (losing more), and come to
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

  // Over a particle's life (time 0..1 = LifeFraction), applied by WriteTo to every particle: multipliers of its own
  // size, colour and intensity. Unset = unchanged. Like Unity's Size / Color over Lifetime modules and Godot's
  // scale_curve / color_ramp. Sampled from their baked tables.
  std::optional<anim::Curve> size_over_life;
  std::optional<anim::Gradient> color_over_life;
  std::optional<anim::Curve> intensity_over_life;
  // Billboards only: a moving particle becomes a streak along its velocity (Unity's Stretched Billboard "speed
  // scale"): stretch = velocity x velocity_scale (seconds), at most max_length long. 0 = round quads.
  struct Stretch {
    float velocity_scale = 0.0f;
    float max_length = 1e9f;
  };
  Stretch stretch;

 private:
  glm::vec3 RandomDirectionInCone(const glm::vec3& axis, float half_angle);

  std::vector<Particle> particles_;
  size_t capacity_;
  core::Random rng_;  // visuals: any sequence would do, but one shared generator is simpler
};

}  // namespace wlw::fx
