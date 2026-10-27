#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <vector>

#include "wlw/core/color.h"
#include "wlw/core/math.h"

namespace wlw::anim {

// A colour (with alpha) that changes over time: "colour over a particle's life", white-hot to orange to a faded red.
// Godot's Gradient (one list of stops, alpha in each), Unity's Gradient (which keeps colour and alpha keys apart).
//
// Stops are (time, Color), kept in time order. Linear blends neighbouring stops channel by channel in the colours'
// authored (sRGB-encoded) values, the way the engine's colours are written; Constant holds each stop's colour until
// the next one (steps). Before the first stop and after the last the colour stays at theirs.
//
//   Gradient({{0.0f, colors::kWhite}, {0.3f, Color::Hex(0xFFB040)}, {1.0f, Color::Hex(0xFF3010).WithAlpha(0)}})
//
// Evaluate(t) is exact; Sample(t) reads a table baked when the stops change (kBakeSize colours across the stops'
// time range): constant time, for per-particle per-frame use. A default Gradient is solid white (a multiplier that
// changes nothing).
class Gradient {
 public:
  struct Stop {
    float time = 0.0f;
    Color color{1.0f, 1.0f, 1.0f, 1.0f};
  };
  enum class Interpolation : uint8_t { Linear, Constant };
  static constexpr size_t kBakeSize = 64;

  Gradient() : Gradient(Solid({1.0f, 1.0f, 1.0f, 1.0f})) {}
  explicit Gradient(std::vector<Stop> stops, Interpolation interpolation = Interpolation::Linear)
      : stops_(std::move(stops)), interpolation_(interpolation) {
    Changed();
  }
  Gradient(std::initializer_list<Stop> stops, Interpolation interpolation = Interpolation::Linear)
      : Gradient(std::vector<Stop>(stops), interpolation) {}

  static Gradient Solid(const Color& color) { return Gradient(std::vector<Stop>{{0.0f, color}}); }

  // The colour at time `t`, exactly.
  Color Evaluate(float t) const {
    if (stops_.empty()) return {1.0f, 1.0f, 1.0f, 1.0f};
    if (t <= stops_.front().time) return stops_.front().color;
    if (t >= stops_.back().time) return stops_.back().color;
    const auto right = std::upper_bound(stops_.begin(), stops_.end(), t,
                                        [](float time, const Stop& stop) { return time < stop.time; });
    const Stop& b = *right;
    const Stop& a = *(right - 1);
    if (interpolation_ == Interpolation::Constant) return a.color;
    const float span = b.time - a.time;
    return span > 0.0f ? Lerp(a.color, b.color, (t - a.time) / span) : b.color;
  }

  // The colour at time `t` from the baked table (linear between its entries; Constant: the nearest entry at or
  // before t, so steps stay sharp to 1/kBakeSize of the range).
  Color Sample(float t) const {
    const float x = std::clamp((t - start_) * scale_, 0.0f, 1.0f) * static_cast<float>(kBakeSize - 1);
    const size_t i = std::min(static_cast<size_t>(x), kBakeSize - 2);
    if (interpolation_ == Interpolation::Constant) return baked_[x >= static_cast<float>(kBakeSize - 1) ? i + 1 : i];
    return Lerp(baked_[i], baked_[i + 1], x - static_cast<float>(i));
  }

  const std::vector<Stop>& Stops() const { return stops_; }
  Interpolation GetInterpolation() const { return interpolation_; }
  void SetStops(std::vector<Stop> stops) {
    stops_ = std::move(stops);
    Changed();
  }
  void SetInterpolation(Interpolation interpolation) {
    interpolation_ = interpolation;
    Changed();
  }

 private:
  void Changed() {
    std::stable_sort(stops_.begin(), stops_.end(), [](const Stop& a, const Stop& b) { return a.time < b.time; });
    start_ = stops_.empty() ? 0.0f : stops_.front().time;
    const float span = stops_.empty() ? 0.0f : stops_.back().time - start_;
    scale_ = span > 0.0f ? 1.0f / span : 0.0f;
    for (size_t i = 0; i < kBakeSize; ++i)
      baked_[i] = Evaluate(start_ + span * static_cast<float>(i) / static_cast<float>(kBakeSize - 1));
  }

  std::vector<Stop> stops_;
  Interpolation interpolation_ = Interpolation::Linear;
  float start_ = 0.0f;
  float scale_ = 0.0f;
  std::array<Color, kBakeSize> baked_{};
};

}  // namespace wlw::anim
