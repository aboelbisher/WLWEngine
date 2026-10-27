#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <initializer_list>
#include <vector>

#include "wlw/core/math.h"

namespace wlw::anim {

// A float that changes over time along a shape drawn with keys: "size over a particle's life", "how hard the
// screen shakes over 0.4 s". Unity's AnimationCurve, Godot's Curve, Unreal's FRichCurve.
//
// Between two keys the value follows a cubic Hermite segment: it leaves the left key with that key's out tangent
// and arrives at the right key with the right key's in tangent (slopes in value per unit of time). Before the
// first key and after the last the value stays at their values. Make one with:
//   Curve::Smooth({{0, 0}, {0.2f, 1}, {1, 0}})  automatic tangents (Catmull-Rom; the end keys flat): a soft shape
//   Curve::Linear({{0, 1}, {1, 0}})             straight lines between the keys (tangents = the segments' slopes)
//   Curve::Constant(1)                          one value everywhere
//   Curve({Curve::Key{...}, ...})               tangents as given
//
// Evaluate(t) is exact (a binary search for the segment). Sample(t) reads a table baked when the keys change
// (kBakeSize points across the keys' time range, linear between them): constant time, for per-particle per-frame
// use. Like Godot's baked curve. A default Curve is Constant(1) (a multiplier that changes nothing).
class Curve {
 public:
  struct Key {
    float time = 0.0f;
    float value = 0.0f;
    float in_tangent = 0.0f;   // slope arriving at the key (value per time unit)
    float out_tangent = 0.0f;  // slope leaving it
  };
  struct Point {  // a key without tangents, for Smooth / Linear
    float time = 0.0f;
    float value = 0.0f;
  };
  static constexpr size_t kBakeSize = 64;

  Curve() : Curve(Constant(1.0f)) {}
  explicit Curve(std::vector<Key> keys) : keys_(std::move(keys)) { Changed(); }
  Curve(std::initializer_list<Key> keys) : Curve(std::vector<Key>(keys)) {}

  static Curve Constant(float value) { return Curve(std::vector<Key>{{0.0f, value, 0.0f, 0.0f}}); }
  static Curve Linear(std::initializer_list<Point> points) {
    std::vector<Key> keys = KeysOf(points);
    for (size_t i = 0; i < keys.size(); ++i) {
      if (i > 0) keys[i].in_tangent = Slope(keys[i - 1], keys[i]);
      if (i + 1 < keys.size()) keys[i].out_tangent = Slope(keys[i], keys[i + 1]);
    }
    return Curve(std::move(keys));
  }
  static Curve Smooth(std::initializer_list<Point> points) {
    std::vector<Key> keys = KeysOf(points);
    // Catmull-Rom: an inner key's slope is the one from its left neighbour to its right one; the ends stay flat
    // (Unity's "Auto" / Godot's smooth tangents look the same away from the ends).
    for (size_t i = 1; i + 1 < keys.size(); ++i) {
      const float slope = Slope(keys[i - 1], keys[i + 1]);
      keys[i].in_tangent = keys[i].out_tangent = slope;
    }
    return Curve(std::move(keys));
  }

  // The value at time `t`, exactly.
  float Evaluate(float t) const {
    if (keys_.empty()) return 0.0f;
    if (t <= keys_.front().time) return keys_.front().value;
    if (t >= keys_.back().time) return keys_.back().value;
    // The first key after t (exists: t is inside the range).
    const auto right = std::upper_bound(keys_.begin(), keys_.end(), t,
                                        [](float time, const Key& key) { return time < key.time; });
    const Key& b = *right;
    const Key& a = *(right - 1);
    const float span = b.time - a.time;
    if (span <= 0.0f) return b.value;
    const float s = (t - a.time) / span;
    // Cubic Hermite basis; the tangents are per time unit, so they scale by the segment's length.
    const float s2 = s * s, s3 = s2 * s;
    const float h00 = 2.0f * s3 - 3.0f * s2 + 1.0f;
    const float h10 = s3 - 2.0f * s2 + s;
    const float h01 = -2.0f * s3 + 3.0f * s2;
    const float h11 = s3 - s2;
    return h00 * a.value + h10 * span * a.out_tangent + h01 * b.value + h11 * span * b.in_tangent;
  }

  // The value at time `t` from the baked table: within a hair of Evaluate for smooth curves, constant time.
  float Sample(float t) const {
    const float x = std::clamp((t - start_) * scale_, 0.0f, 1.0f) * static_cast<float>(kBakeSize - 1);
    const size_t i = std::min(static_cast<size_t>(x), kBakeSize - 2);
    return Lerp(baked_[i], baked_[i + 1], x - static_cast<float>(i));
  }

  const std::vector<Key>& Keys() const { return keys_; }
  // Adds a key (kept in time order) / replaces them all; the table is baked again.
  void AddKey(const Key& key) {
    keys_.insert(std::upper_bound(keys_.begin(), keys_.end(), key.time,
                                  [](float time, const Key& k) { return time < k.time; }),
                 key);
    Changed();
  }
  void SetKeys(std::vector<Key> keys) {
    keys_ = std::move(keys);
    Changed();
  }

 private:
  static std::vector<Key> KeysOf(std::initializer_list<Point> points) {
    std::vector<Key> keys;
    keys.reserve(points.size());
    for (const Point& p : points) keys.push_back({p.time, p.value, 0.0f, 0.0f});
    return keys;
  }
  static float Slope(const Key& a, const Key& b) {
    return b.time > a.time ? (b.value - a.value) / (b.time - a.time) : 0.0f;
  }

  void Changed() {
    std::stable_sort(keys_.begin(), keys_.end(), [](const Key& a, const Key& b) { return a.time < b.time; });
    start_ = keys_.empty() ? 0.0f : keys_.front().time;
    const float span = keys_.empty() ? 0.0f : keys_.back().time - start_;
    scale_ = span > 0.0f ? 1.0f / span : 0.0f;
    for (size_t i = 0; i < kBakeSize; ++i)
      baked_[i] = Evaluate(start_ + span * static_cast<float>(i) / static_cast<float>(kBakeSize - 1));
  }

  std::vector<Key> keys_;
  float start_ = 0.0f;  // the first key's time
  float scale_ = 0.0f;  // 1 / the keys' time span (0: one key)
  std::array<float, kBakeSize> baked_{};
};

}  // namespace wlw::anim
