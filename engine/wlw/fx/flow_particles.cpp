#include "wlw/fx/flow_particles.h"

#include <algorithm>
#include <cmath>

#include "wlw/core/assert.h"
#include "wlw/core/profile.h"

namespace wlw::fx {

namespace {
constexpr int kMaxSegments = 4096;  // resampling detail per path, at most
}  // namespace

FlowParticles::FlowParticles(size_t capacity, uint32_t seed) : capacity_(capacity), rng_(seed) {
  // The only allocations: dots never grow the arrays past the capacity.
  path_.reserve(capacity);
  dist_.reserve(capacity);
  lane_.reserve(capacity);
  phase_sin_.reserve(capacity);
  phase_cos_.reserve(capacity);
  speed_.reserve(capacity);
  size_.reserve(capacity);
  color_.reserve(capacity);
  place_.reserve(capacity);
}

int FlowParticles::AddPath(std::span<const glm::vec2> points, const FlowPathSettings& settings) {
  Path path;
  path.settings = settings;

  // Arc length along the given polyline (repeated points add nothing).
  std::vector<float> along(points.size(), 0.0f);
  for (size_t i = 1; i < points.size(); ++i) along[i] = along[i - 1] + glm::length(points[i] - points[i - 1]);
  path.length = points.empty() ? 0.0f : along.back();
  if (!WLW_VERIFY(points.size() >= 2 && path.length > 0.0f, "a flow path needs 2+ points and some length")) {
    path.points = {points.empty() ? glm::vec2(0.0f) : points[0]};
    path.points.push_back(path.points[0]);
    path.normals = {glm::vec2(0.0f, 1.0f), glm::vec2(0.0f, 1.0f)};
    path.length = 0.0f;
    paths_.push_back(std::move(path));
    return static_cast<int>(paths_.size() - 1);
  }

  // Resample at an even step (about half the width: smooth enough to bend, few points), so the
  // point at a distance is one division away.
  const float wanted_step = std::max(settings.width * 0.5f, path.length / kMaxSegments);
  const int segments = std::max(1, static_cast<int>(std::ceil(path.length / std::max(wanted_step, 1e-6f))));
  path.step = path.length / static_cast<float>(segments);
  path.points.reserve(static_cast<size_t>(segments) + 1);
  size_t source = 0;
  for (int i = 0; i <= segments; ++i) {
    const float d = std::min(static_cast<float>(i) * path.step, path.length);
    while (source + 2 < points.size() && along[source + 1] < d) ++source;
    const float span = along[source + 1] - along[source];
    const float t = span > 0.0f ? std::clamp((d - along[source]) / span, 0.0f, 1.0f) : 0.0f;
    path.points.push_back(glm::mix(points[source], points[source + 1], t));
  }

  // Normals from the neighbours on either side (one-sided at the ends): smooth through corners.
  path.normals.resize(path.points.size());
  for (size_t i = 0; i < path.points.size(); ++i) {
    const glm::vec2 a = path.points[i == 0 ? 0 : i - 1];
    const glm::vec2 b = path.points[std::min(i + 1, path.points.size() - 1)];
    const glm::vec2 tangent = b - a;
    const float length = glm::length(tangent);
    path.normals[i] = length > 0.0f ? glm::vec2(-tangent.y, tangent.x) / length : glm::vec2(0.0f, 1.0f);
  }
  paths_.push_back(std::move(path));
  return static_cast<int>(paths_.size() - 1);
}

glm::vec2 FlowParticles::PointAt(int path, float distance) const {
  const Path& p = paths_[Index(path)];
  const float f = std::clamp(distance, 0.0f, p.length) / p.step;
  const auto last = static_cast<int>(p.points.size()) - 2;
  const int i = std::clamp(static_cast<int>(f), 0, std::max(last, 0));
  return glm::mix(p.points[static_cast<size_t>(i)], p.points[static_cast<size_t>(i) + 1], f - static_cast<float>(i));
}

void FlowParticles::SetStop(int path, float distance) {
  Path& p = paths_[Index(path)];
  if (distance == p.stop) return;
  p.stop = distance;
  if (p.queued == 0) return;
  // Release the line: everyone waiting here moves again (and re-queues if there's still a stop).
  p.queued = 0;
  p.line = 0.0f;
  for (size_t i = 0; i < path_.size(); ++i) {
    if (path_[i] == path) place_[i] = -1;
  }
}

void FlowParticles::SetNext(int path, std::span<const int> next) {
  paths_[Index(path)].next.assign(next.begin(), next.end());
  paths_[Index(path)].next_turn = 0;
  for (Path& p : paths_) p.fed = false;
  for (const Path& p : paths_) {
    for (const int n : p.next) paths_[Index(n)].fed = true;
  }
}

void FlowParticles::SetEmitter(int path, float per_second, Color color) {
  Path& p = paths_[Index(path)];
  p.emit_rate = std::max(per_second, 0.0f);
  p.emit_color = Color32(color).Packed();
}

bool FlowParticles::Emit(int path, float distance, Color color) {
  if (dist_.size() >= capacity_) return false;
  Spawn(path, std::clamp(distance, 0.0f, paths_[Index(path)].length), Color32(color).Packed());
  return true;
}

void FlowParticles::Spawn(int path, float distance, uint32_t color) {
  path_.push_back(path);
  dist_.push_back(distance);
  lane_.push_back(rng_.Range(-1.0f, 1.0f));
  const float phase = rng_.Range(0.0f, 6.2831853f);
  phase_sin_.push_back(std::sin(phase));
  phase_cos_.push_back(std::cos(phase));
  speed_.push_back(1.0f + rng_.Range(-speed_jitter, speed_jitter));
  size_.push_back(1.0f + rng_.Range(-size_jitter, size_jitter));
  color_.push_back(color);
  place_.push_back(-1);
}

void FlowParticles::Remove(size_t i) {
  auto take_last = [i](auto& array) {
    array[i] = array.back();
    array.pop_back();
  };
  take_last(path_);
  take_last(dist_);
  take_last(lane_);
  take_last(phase_sin_);
  take_last(phase_cos_);
  take_last(speed_);
  take_last(size_);
  take_last(color_);
  take_last(place_);
}

void FlowParticles::Clear() {
  for (auto* array : {&dist_, &lane_, &phase_sin_, &phase_cos_, &speed_, &size_}) array->clear();
  path_.clear();
  color_.clear();
  place_.clear();
  for (Path& p : paths_) {
    p.queued = 0;
    p.line = 0.0f;
    p.emit_debt = 0.0f;
  }
}

void FlowParticles::Update(float dt) {
  WLW_PROFILE_SCOPE("FlowParticles::Update");
  if (dt <= 0.0f) return;
  time_ += dt;
  for (Path& p : paths_) {
    const float wave = std::fmod(time_ * p.settings.wobble_hz, 1.0f) * 6.2831853f;  // whole turns dropped
    p.wave_sin = std::sin(wave);
    p.wave_cos = std::cos(wave);
  }

  // Move. A dot in a queue stays put; one that reaches the back of its path's line joins it; one
  // past the end moves on to the next path (keeping what it overshot) or is removed.
  for (size_t i = 0; i < dist_.size();) {
    if (place_[i] >= 0) {
      ++i;
      continue;
    }
    Path& p = paths_[Index(path_[i])];
    float d = dist_[i] + p.settings.speed * speed_[i] * dt;
    if (p.stop <= p.length) {
      // The line's length, not its count: dots that joined at another density keep their places.
      const float back = p.stop - p.line;
      if (d >= back) {
        place_[i] = p.queued++;
        p.line += 1.0f / (p.settings.packed_per_unit * density_);
        dist_[i] = std::max(back, 0.0f);  // a line longer than the path piles up at its start
        ++i;
        continue;
      }
    }
    if (d >= p.length) {
      if (p.next.empty()) {
        Remove(i);  // the last dot takes slot i and is moved in this same iteration
        continue;
      }
      const int next = p.next[p.next_turn++ % p.next.size()];
      d = std::min(d - p.length, paths_[Index(next)].length);
      path_[i] = next;
    }
    dist_[i] = d;
    ++i;
  }

  // Emitters: new dots near the start, spread over this frame's travel so they don't come in clumps.
  for (size_t pi = 0; pi < paths_.size(); ++pi) {
    Path& p = paths_[pi];
    if (p.emit_rate <= 0.0f) continue;
    p.emit_debt += p.emit_rate * density_ * dt;
    while (p.emit_debt >= 1.0f) {
      p.emit_debt -= 1.0f;
      if (dist_.size() >= capacity_) {
        p.emit_debt = 0.0f;  // full: the missed dots are dropped, not owed
        break;
      }
      Spawn(static_cast<int>(pi), rng_.Range(0.0f, std::min(p.settings.speed * dt, p.length)), p.emit_color);
    }
  }
}

void FlowParticles::WriteTo(scene::SpriteBatch& batch) const {
  WLW_PROFILE_SCOPE("FlowParticles::WriteTo");
  const std::span<rendering::SpriteInstance> out = batch.Write(dist_.size());
  glm::vec2 lo(std::numeric_limits<float>::max()), hi(std::numeric_limits<float>::lowest());
  const float inv_fade = fade_length > 0.0f ? 1.0f / fade_length : 0.0f;
  for (size_t i = 0; i < out.size(); ++i) {
    const Path& p = paths_[Index(path_[i])];
    const float d = dist_[i];
    // Point and normal at d, from the two samples around it.
    const float f = d / p.step;
    const int last = static_cast<int>(p.points.size()) - 2;
    const int k = std::clamp(static_cast<int>(f), 0, last);
    const float t = f - static_cast<float>(k);
    const size_t a = static_cast<size_t>(k);
    const glm::vec2 point = p.points[a] + (p.points[a + 1] - p.points[a]) * t;
    const glm::vec2 normal = p.normals[a] + (p.normals[a + 1] - p.normals[a]) * t;
    // Across: its lane, drifting slowly (the wobble keeps it inside the width).
    const float wobble = p.settings.wobble;
    const float side = lane_[i] * (1.0f - wobble) + wobble * (phase_sin_[i] * p.wave_cos + phase_cos_[i] * p.wave_sin);
    const glm::vec2 position = point + normal * (side * 0.5f * p.settings.width);

    float size = dot_size * size_[i];
    if (inv_fade > 0.0f) {
      if (!p.fed) size *= std::min(d * inv_fade, 1.0f);
      if (p.next.empty()) size *= std::clamp((p.length - d) * inv_fade, 0.0f, 1.0f);
    }
    out[i] = {position, size, color_[i]};
    lo = glm::min(lo, position);
    hi = glm::max(hi, position);
  }
  if (out.empty()) {
    batch.SetBounds(glm::vec2(0.0f), glm::vec2(0.0f));
    return;
  }
  const float margin = 0.5f * dot_size * (1.0f + size_jitter);
  batch.SetBounds(lo - margin, hi + margin);
}

}  // namespace wlw::fx
