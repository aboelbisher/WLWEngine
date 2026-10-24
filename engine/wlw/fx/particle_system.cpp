#include "wlw/fx/particle_system.h"

#include <algorithm>
#include <cmath>
#include <span>

#include "wlw/core/math.h"
#include "wlw/core/profile.h"

namespace wlw::fx {

float ParticleSystem::Particle::Size() const {
  const float t = lifetime > 0.0f ? std::clamp(age / lifetime, 0.0f, 1.0f) : 1.0f;
  return size_start + (size_end - size_start) * t;
}

ParticleSystem::ParticleSystem(size_t capacity, uint32_t seed) : capacity_(capacity), rng_(seed) {
  particles_.reserve(capacity);  // the only allocation: bursts never grow the array
}

glm::vec3 ParticleSystem::RandomDirectionInCone(const glm::vec3& axis, float half_angle) {
  // Uniform over the cone's cap: pick cos(theta) uniformly in [cos(half_angle), 1] (picking the
  // angle itself uniformly would bunch directions up around the axis), and a random turn phi.
  const float cos_max = std::cos(std::clamp(half_angle, 0.0f, kPi));
  const float cos_theta = rng_.Range(cos_max, 1.0f);
  const float sin_theta = std::sqrt(std::max(0.0f, 1.0f - cos_theta * cos_theta));
  const float phi = rng_.Range(0.0f, kTau);

  // Any two unit vectors perpendicular to the axis (and each other) to build the direction from.
  const glm::vec3 helper = std::abs(axis.z) < 0.99f ? kUp : kRight;
  const glm::vec3 u = glm::normalize(glm::cross(axis, helper));
  const glm::vec3 v = glm::cross(axis, u);
  return axis * cos_theta + (u * std::cos(phi) + v * std::sin(phi)) * sin_theta;
}

bool ParticleSystem::Emit(const Particle& particle) {
  if (particles_.size() >= capacity_) return false;  // full: like a burst, it's cut, not grown
  particles_.push_back(particle);
  return true;
}

int ParticleSystem::Burst(const BurstSettings& settings, const glm::vec3& origin) {
  const int wanted = settings.count_min +
                     static_cast<int>(rng_.Unit() * static_cast<float>(settings.count_max - settings.count_min + 1));
  const int room = static_cast<int>(capacity_ - particles_.size());
  const int count = std::clamp(wanted, 0, room);  // never beyond the capacity
  const float length = glm::length(settings.direction);
  const glm::vec3 axis = length > 0.0f ? settings.direction / length : kUp;
  float spread = settings.spread;
  float max_spin = settings.max_spin;

  for (int i = 0; i < count; ++i) {
    Particle p;
    const float j = settings.position_jitter;
    p.position = origin + glm::vec3(rng_.Range(-j, j), rng_.Range(-j, j), rng_.Range(-j, j));
    p.velocity = RandomDirectionInCone(axis, spread) * rng_.Range(settings.speed_min, settings.speed_max);
    p.angles = glm::vec3(rng_.Range(0.0f, kTau), rng_.Range(0.0f, kTau), rng_.Range(0.0f, kTau));
    const float s = max_spin;
    p.angular_velocity = glm::vec3(rng_.Range(-s, s), rng_.Range(-s, s), rng_.Range(-s, s));
    p.size_start = settings.size_start;
    p.size_end = settings.size_end;
    const float b = 1.0f + rng_.Range(-settings.brightness_jitter, settings.brightness_jitter);
    p.color = {std::min(settings.color.r * b, 1.0f), std::min(settings.color.g * b, 1.0f),
               std::min(settings.color.b * b, 1.0f), settings.color.a};
    p.lifetime = rng_.Range(settings.lifetime_min, settings.lifetime_max);
    p.age = -settings.delay;
    particles_.push_back(p);
  }
  return count;
}

void ParticleSystem::Update(float dt) {
  WLW_PROFILE_SCOPE("ParticleSystem::Update");
  const float damping = std::exp(-drag * dt);
  for (size_t i = 0; i < particles_.size();) {
    Particle& p = particles_[i];
    if (p.age < 0.0f) {  // still waiting for its delay
      p.age += dt;
      ++i;
      continue;
    }
    // Semi-implicit Euler: update velocity first, then move with the new velocity. Stable and
    // cheap, which is all a short-lived visual effect needs.
    p.velocity = (p.velocity + gravity * dt) * damping;
    const float bottom_before = p.position.z - p.Size() * 0.5f;
    p.position += p.velocity * dt;
    p.angles += p.angular_velocity * dt;
    if (floor) {
      // The floor stops a particle that was above it and is now below it, inside its rectangle.
      // (One that went past an edge and fell below stays below: it never pops back up.)
      const float half = p.Size() * 0.5f;
      const bool inside = p.position.x >= floor->min.x && p.position.x <= floor->max.x &&
                          p.position.y >= floor->min.y && p.position.y <= floor->max.y;
      if (inside && bottom_before >= floor->height - 1e-4f && p.position.z - half < floor->height) {
        p.position.z = floor->height + half;
        const float keep = 1.0f - std::clamp(floor->friction, 0.0f, 1.0f);
        p.velocity = {p.velocity.x * keep, p.velocity.y * keep, -p.velocity.z * floor->bounce};
        p.angular_velocity *= keep;
        if (p.velocity.z < 0.6f) p.velocity.z = 0.0f;  // too slow to hop again: it rests (and slides)
      }
    }
    p.age += dt;
    if (p.age >= p.lifetime) {
      p = particles_.back();  // swap-remove: the last particle takes this slot...
      particles_.pop_back();  // ...and is checked in this same iteration (no ++i)
    } else {
      ++i;
    }
  }
}

namespace {

// The particle's copy: the rows of translate(position) * rotate x * rotate y * rotate z * scale(size)
// (glm's order, the one the particles always had), written out instead of 4 matrix products. With
// Rz = [cz -sz 0; sz cz 0; 0 0 1], Ry = [cy 0 sy; 0 1 0; -sy 0 cy], Rx = [1 0 0; 0 cx -sx; 0 sx cx]:
// Ry * Rz = [cy*cz -cy*sz sy; sz cz 0; -sy*cz sy*sz cy], and Rx mixes its rows 1 and 2.
void WriteInstance(const ParticleSystem::Particle& p, float size, rendering::Instance& out) {
  const auto [sx, cx] = core::SinCos(p.angles.x);
  const auto [sy, cy] = core::SinCos(p.angles.y);
  const auto [sz, cz] = core::SinCos(p.angles.z);
  const float sxsy = sx * sy, cxsy = cx * sy;
  out.rows[0] = {size * (cy * cz), size * (-cy * sz), size * sy, p.position.x};
  out.rows[1] = {size * (sxsy * cz + cx * sz), size * (cx * cz - sxsy * sz), size * (-sx * cy), p.position.y};
  out.rows[2] = {size * (sx * sz - cxsy * cz), size * (cxsy * sz + sx * cz), size * (cx * cy), p.position.z};
  out.color = Color32(p.color).Packed();
}

}  // namespace

void ParticleSystem::WriteTo(scene::MultiMeshInstance3D& node) const {
  WLW_PROFILE_SCOPE("ParticleSystem::WriteTo");
  node.ReserveInstances(capacity_);  // allocates once: later writes stay within it
  const std::span<rendering::Instance> out = node.WriteInstances(particles_.size());
  size_t count = 0;
  for (const Particle& p : particles_) {
    if (p.age < 0.0f) continue;  // still delayed
    const float size = p.Size();
    if (size <= 0.0f) continue;
    WriteInstance(p, size, out[count++]);
  }
  if (count < out.size()) node.WriteInstances(count);  // drop the slots of the skipped ones
}

}  // namespace wlw::fx
