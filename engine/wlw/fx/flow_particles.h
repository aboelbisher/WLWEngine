#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <vector>

#include <glm/glm.hpp>

#include "wlw/core/color.h"
#include "wlw/core/random.h"
#include "wlw/scene/sprite_batch.h"

namespace wlw::fx {

// How dots flow along one path.
struct FlowPathSettings {
  float width = 0.2f;             // dots spread across this, side to side (node units)
  float speed = 1.0f;             // units per second along the path (SetSpeed changes it)
  float packed_per_unit = 40.0f;  // dots per unit of path length when they queue at a stop
  float wobble = 0.3f;            // slow side-to-side drift, as a share of the half width (0..1)
  float wobble_hz = 0.5f;         // how fast it drifts
};

// Many dots (tens of thousands) flowing along paths: liquid in pipes, conveyors, traffic. Not a
// fluid simulation: each dot is "how far along which path, how far to the side", which is all a
// drawn flow needs, so updating one costs a few multiply-adds.
//
// The data is SoA, one array per attribute (like Niagara's particle data sets or Unity's Burst
// particle jobs), not one struct per dot: each loop reads only the arrays it needs, contiguously,
// and the compiler can vectorize it. Output goes to a scene::SpriteBatch (WriteTo: 16 bytes per
// dot, one draw call).
//
// Paths are polylines, resampled at even spacing when added, so finding the point at a distance
// is one division, not a search. What happens at a path's end is routing (SetNext): the dot moves
// on to the next path, or fades out at a dead end. A stop (SetStop: a closed gate) makes dots
// queue: each one reaching it takes the next place in line, so the line grows back up the path
// (a blocked pipe visibly fills); opening it lets them all go.
//
// It only DRAWS a flow: the game's rules (which gate is open, what colour goes where) live in the
// game and set speeds, stops and emitters here. Randomness is seeded (core::Random).
class FlowParticles {
 public:
  static constexpr float kOpen = std::numeric_limits<float>::infinity();

  explicit FlowParticles(size_t capacity = 20000, uint32_t seed = 0x9E3779B9u);

  // ---- Paths ----
  // A path through `points` (at least 2, node units). Returns its id (0, 1, 2...).
  int AddPath(std::span<const glm::vec2> points, const FlowPathSettings& settings = {});
  size_t PathCount() const { return paths_.size(); }
  float PathLength(int path) const { return paths_[Index(path)].length; }
  glm::vec2 PointAt(int path, float distance) const;  // on the centre line, clamped to the path

  void SetSpeed(int path, float units_per_second) { paths_[Index(path)].settings.speed = units_per_second; }
  // A stop at `distance` from the path's start (kOpen = none): dots queue up before it. Moving or
  // removing it releases the queue.
  void SetStop(int path, float distance);
  float Stop(int path) const { return paths_[Index(path)].stop; }
  int Queued(int path) const { return paths_[Index(path)].queued; }  // dots waiting at its stop
  // Where dots go at the end: the next paths in turn (round robin). Empty = they fade out.
  void SetNext(int path, std::span<const int> next);
  // Spawns `per_second` dots of `color` at the path's start (0 = none). Fractions carry over.
  void SetEmitter(int path, float per_second, core::Color color);

  // How many dots, as a share of what the paths ask for (0..1; a quality level): emitters spawn
  // this share of their rate and queues pack this share per unit, so a blocked pipe still fills
  // at the same speed. Dots already out finish their way (no pop); the look is the game's, e.g.
  // fewer dots drawn bigger (dot_size). Emit() isn't scaled.
  void SetDensity(float density) { density_ = std::clamp(density, 0.01f, 1.0f); }
  float Density() const { return density_; }

  // ---- Dots ----
  // One dot at `distance` along `path` (a random side offset). False when full.
  bool Emit(int path, float distance, core::Color color);
  void Update(float dt);
  void Clear();
  size_t Count() const { return dist_.size(); }
  size_t Capacity() const { return capacity_; }

  // Every dot into `batch` (cut at its capacity), with the bounds around them.
  void WriteTo(scene::SpriteBatch& batch) const;

  // For tests and tools: one dot's state.
  struct Dot {
    int path;
    float distance;
    int place;  // in its path's queue, -1 = moving
    uint32_t color;
  };
  Dot GetDot(size_t i) const { return {path_[i], dist_[i], place_[i], color_[i]}; }

  float dot_size = 0.12f;      // diameter (node units)
  float size_jitter = 0.2f;    // each dot's size is dot_size x (1 -+ this)
  float speed_jitter = 0.15f;  // each dot's speed is the path's x (1 -+ this)
  float fade_length = 0.15f;   // dots grow over this distance at a path nothing feeds, shrink at a dead end

 private:
  struct Path {
    std::vector<glm::vec2> points;   // resampled: `step` apart, first = start, last = end
    std::vector<glm::vec2> normals;  // unit, to the left of the direction of travel
    float step = 1.0f;
    float length = 0.0f;
    FlowPathSettings settings;
    float stop = kOpen;
    int queued = 0;
    float line = 0.0f;  // the queue's length along the path
    std::vector<int> next;
    uint32_t next_turn = 0;
    bool fed = false;  // some path's next is this one (dots arrive: no fade-in at the start)
    float emit_rate = 0.0f;
    float emit_debt = 0.0f;
    uint32_t emit_color = 0xFFFFFFFFu;
    float wave_sin = 0.0f, wave_cos = 1.0f;  // sin / cos of its wobble angle now (once per path per frame)
  };

  size_t Index(int path) const { return static_cast<size_t>(path); }
  void Spawn(int path, float distance, uint32_t color);
  void Remove(size_t i);  // swap-remove in every array

  std::vector<Path> paths_;
  size_t capacity_;
  core::Random rng_;
  float time_ = 0.0f;  // drives the wobble
  float density_ = 1.0f;

  // One entry per dot in each (SoA).
  std::vector<int> path_;
  std::vector<float> dist_;       // along its path
  std::vector<float> lane_;       // -1..1: where across the width
  std::vector<float> phase_sin_;  // sin and cos of the dot's wobble phase, made once: then the wobble
  std::vector<float> phase_cos_;  //   is sin(phase + wave) = sin p cos w + cos p sin w, no sin per dot
  std::vector<float> speed_;      // multiplier on the path's speed
  std::vector<float> size_;       // multiplier on dot_size
  std::vector<uint32_t> color_;   // RGBA8 (rendering::PackColor)
  std::vector<int> place_;        // place in its path's queue, -1 = moving
};

}  // namespace wlw::fx
