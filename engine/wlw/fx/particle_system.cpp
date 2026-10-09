#include "wlw/fx/particle_system.h"

#include <algorithm>
#include <cmath>

#include <glm/gtc/matrix_transform.hpp>

namespace wlw::fx {

float ParticleSystem::Particle::Size() const {
  const float t = lifetime > 0.0f ? std::clamp(age / lifetime, 0.0f, 1.0f) : 1.0f;
  return size_start + (size_end - size_start) * t;
}

ParticleSystem::ParticleSystem(size_t capacity, uint32_t seed)
    : capacity_(capacity), rng_(seed) {
  particles_.reserve(capacity);  // the only allocation: bursts never grow the array
}

glm::vec3 ParticleSystem::RandomDirectionInCone(const glm::vec3& axis, float half_angle_degrees) {
  // Uniform over the cone's cap: pick cos(theta) uniformly in [cos(half_angle), 1] (picking the
  // angle itself uniformly would bunch directions up around the axis), and a random turn phi.
  const float cos_max = std::cos(glm::radians(std::clamp(half_angle_degrees, 0.0f, 180.0f)));
  const float cos_theta = rng_.Range(cos_max, 1.0f);
  const float sin_theta = std::sqrt(std::max(0.0f, 1.0f - cos_theta * cos_theta));
  const float phi = rng_.Range(0.0f, 6.2831853f);

  // Any two unit vectors perpendicular to the axis (and each other) to build the direction from.
  const glm::vec3 helper = std::abs(axis.y) < 0.99f ? glm::vec3(0, 1, 0) : glm::vec3(1, 0, 0);
  const glm::vec3 u = glm::normalize(glm::cross(axis, helper));
  const glm::vec3 v = glm::cross(axis, u);
  return axis * cos_theta + (u * std::cos(phi) + v * std::sin(phi)) * sin_theta;
}

int ParticleSystem::Burst(const BurstSettings& settings, const glm::vec3& origin) {
  const int wanted = settings.count_min +
                     static_cast<int>(rng_.Unit() * static_cast<float>(settings.count_max - settings.count_min + 1));
  const int room = static_cast<int>(capacity_ - particles_.size());
  const int count = std::clamp(wanted, 0, room);  // never beyond the capacity
  const float length = glm::length(settings.direction);
  const glm::vec3 axis = length > 0.0f ? settings.direction / length : glm::vec3(0, 1, 0);

  for (int i = 0; i < count; ++i) {
    Particle p;
    const float j = settings.position_jitter;
    p.position = origin + glm::vec3(rng_.Range(-j, j), rng_.Range(-j, j), rng_.Range(-j, j));
    p.velocity = RandomDirectionInCone(axis, settings.spread_degrees) * rng_.Range(settings.speed_min, settings.speed_max);
    p.rotation = glm::vec3(rng_.Range(0, 360), rng_.Range(0, 360), rng_.Range(0, 360));
    const float s = settings.spin_max;
    p.spin = glm::vec3(rng_.Range(-s, s), rng_.Range(-s, s), rng_.Range(-s, s));
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
    p.position += p.velocity * dt;
    p.rotation += p.spin * dt;
    p.age += dt;
    if (p.age >= p.lifetime) {
      p = particles_.back();  // swap-remove: the last particle takes this slot...
      particles_.pop_back();  // ...and is checked in this same iteration (no ++i)
    } else {
      ++i;
    }
  }
}

void ParticleSystem::BuildInstances(std::vector<rendering::InstanceData>& out) const {
  out.clear();
  for (const Particle& p : particles_) {
    if (p.age < 0.0f) continue;
    const float size = p.Size();
    if (size <= 0.0f) continue;
    glm::mat4 m = glm::translate(glm::mat4(1.0f), p.position);
    m = glm::rotate(m, glm::radians(p.rotation.x), glm::vec3(1, 0, 0));
    m = glm::rotate(m, glm::radians(p.rotation.y), glm::vec3(0, 1, 0));
    m = glm::rotate(m, glm::radians(p.rotation.z), glm::vec3(0, 0, 1));
    m = glm::scale(m, glm::vec3(size));
    out.push_back({m, p.color});
  }
}

}  // namespace wlw::fx
