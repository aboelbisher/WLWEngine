#include "wlw/fx/trail.h"

#include <algorithm>

namespace wlw::fx {

Trail::Trail(size_t capacity) : capacity_(std::max<size_t>(capacity, 2)) { points_.reserve(capacity_); }

void Trail::Push(const glm::vec3& head) {
  // The newest point follows the head while it is still close to the one before it.
  if (points_.size() >= 2 && settings.min_distance > 0.0f &&
      glm::distance(points_[points_.size() - 2].position, head) < settings.min_distance) {
    points_.back() = {head, 0.0f};
    return;
  }
  if (points_.size() == capacity_) points_.erase(points_.begin());  // full: the oldest goes (within the reserve)
  points_.push_back({head, 0.0f});
}

void Trail::Update(float dt) {
  if (dt <= 0.0f) return;
  for (Point& p : points_) p.age += dt;
  // Keep the oldest point while the next one is younger than the lifetime: the tail is cut between them.
  const float lifetime = std::max(settings.lifetime, 0.0f);
  size_t gone = 0;
  while (gone + 1 < points_.size() && points_[gone + 1].age >= lifetime) ++gone;
  if (gone + 1 == points_.size() && points_.back().age >= lifetime) gone = points_.size();  // all of it
  points_.erase(points_.begin(), points_.begin() + static_cast<std::ptrdiff_t>(gone));
}

size_t Trail::Write(std::span<rendering::BillboardInstance> out) const {
  const float lifetime = settings.lifetime;
  if (points_.size() < 2 || lifetime <= 0.0f) return 0;
  size_t count = 0;
  for (size_t i = points_.size() - 1; i > 0 && count < out.size(); --i) {
    const Point& newer = points_[i];
    Point older = points_[i - 1];
    if (newer.age >= lifetime) break;
    if (older.age > lifetime) {  // the tail's end: cut where the trail is exactly `lifetime` old
      const float s = (lifetime - newer.age) / (older.age - newer.age);
      older = {newer.position + (older.position - newer.position) * s, lifetime};
    }
    const glm::vec3 along = newer.position - older.position;
    if (glm::dot(along, along) < 1e-10f) continue;
    const float t = 0.5f * (newer.age + older.age) / lifetime;  // the segment's middle, 0 = head .. 1 = end
    Color color = settings.color;
    if (settings.color_over_trail) color *= settings.color_over_trail->Sample(t);
    rendering::BillboardInstance& b = out[count++];
    b.position = 0.5f * (newer.position + older.position);
    b.size = settings.width * settings.width_over_trail.Sample(t);
    b.stretch = along;
    b.rotation = 0.0f;
    b.color = Color32(color).Packed();
    b.intensity = settings.intensity * settings.intensity_over_trail.Sample(t);
  }
  return count;
}

}  // namespace wlw::fx
