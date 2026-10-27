#pragma once

#include <cstddef>
#include <optional>
#include <span>
#include <vector>

#include <glm/glm.hpp>

#include "wlw/anim/curve.h"
#include "wlw/anim/gradient.h"
#include "wlw/core/color.h"
#include "wlw/rendering/instance_buffer.h"

namespace wlw::fx {

// How a Trail looks. Along the trail, 0 = the head (newest) and 1 = the tail's end (`lifetime` old): the curves and
// the gradient multiply the width, intensity and colour there.
struct TrailSettings {
  float lifetime = 0.15f;     // seconds a point stays: the trail's length in time (a faster head = a longer trail)
  float min_distance = 0.0f;  // a new point only this far from the last kept one; closer, the newest just follows
                              // the head (Unity's Min Vertex Distance): fewer, longer segments
  float width = 0.2f;         // across, at the head (node units)
  Color color{1.0f, 1.0f, 1.0f, 1.0f};
  float intensity = 1.0f;  // multiplies rgb on the GPU (HDR: past 1 feeds bloom)
  anim::Curve width_over_trail = anim::Curve::Linear({{0.0f, 1.0f}, {1.0f, 0.0f}});
  anim::Curve intensity_over_trail = anim::Curve::Linear({{0.0f, 1.0f}, {1.0f, 0.0f}});
  std::optional<anim::Gradient> color_over_trail;  // unset = `color` all along
};

// The last moments of a moving point's path, drawn as light behind it: a comet's tail, a homing spark's streak.
// The game pushes the head's position every frame; the trail keeps those points for `lifetime` seconds and
// writes one stretched billboard per segment between two of them (rendering::BillboardInstance, into a
// scene::BillboardBatch the game owns), so it follows the curve the head took, thinning and fading towards the end.
//
// Unity's TrailRenderer and Unreal's ribbon emitters build a ribbon MESH (a strip of quads joined edge to edge);
// this chains camera-facing quads instead: no new renderer or vertex format, one draw with everything else in the
// batch, and any camera angle. The price: neighbouring quads overlap by their round ends, so an additive trail is
// a little brighter at the joints (dense points, from a head moving a fraction of the width per frame, hide it).
//
//   fx::Trail trail(24);
//   trail.settings = {.lifetime = 0.12f, .width = 0.3f, .intensity = 2.0f};
//   // every frame:
//   trail.Update(dt);
//   trail.Push(orb_position);
//   used += trail.Write(out.subspan(used));  // out = batch.Write(capacity); then batch.Write(used), FitBounds()
//
// Fixed capacity (points): when full, the oldest point goes. Push and Update never allocate.
class Trail {
 public:
  explicit Trail(size_t capacity = 32);

  TrailSettings settings;

  // The head is at `head` now. Adds a point (age 0), or moves the newest one there (see min_distance).
  void Push(const glm::vec3& head);
  // Ages every point by `dt` seconds and drops the ones the trail has left behind: a point goes once the one after
  // it is `lifetime` old too (the oldest kept point marks where the tail is cut, between two points).
  void Update(float dt);
  void Clear() { points_.clear(); }

  // One quad per segment, from the head backwards (segments shorter than 1e-5 units are skipped), into `out`.
  // Returns how many it wrote (at most out.size(), at most SegmentCount()).
  size_t Write(std::span<rendering::BillboardInstance> out) const;

  size_t PointCount() const { return points_.size(); }
  size_t SegmentCount() const { return points_.empty() ? 0 : points_.size() - 1; }
  size_t Capacity() const { return capacity_; }
  bool Empty() const { return points_.size() < 2; }  // nothing to draw

 private:
  struct Point {
    glm::vec3 position{0.0f};
    float age = 0.0f;  // seconds since it was the head
  };
  size_t capacity_;
  std::vector<Point> points_;  // oldest first
};

}  // namespace wlw::fx
