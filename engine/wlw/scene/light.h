#pragma once

#include "wlw/core/color.h"
#include "wlw/core/deprecated.h"
#include "wlw/core/math.h"
#include "wlw/core/rotation.h"
#include "wlw/scene/node.h"

namespace wlw::scene {

// How a light's shadows look (only directional lights cast shadows for now).
struct ShadowSettings {
  int map_size = 1024;    // the shadow map is map_size x map_size depth texels
  float softness = 1.5f;  // edge blur: radius of the PCF filter, in shadow-map texels
  float strength = 0.6f;  // 0 = no darkening, 1 = only the ambient light is left in shadow
};

// A light source is a node: added to a scene (Root().AddChild) like geometry, moved and
// turned like any node (and with its parent), hidden with SetVisible(false). It has no model, so
// nothing is drawn for it; the renderer gathers the visible lights every frame and lights the
// scene with them (up to kMaxLights of each type).
//
// Directional and spot lights shine along the node's forward axis, -Z, turned by its rotation.
// SetDirection is the convenient way to aim them. Like Godot's Light3D.
class Light3D : public Node3D {
 public:
  enum class Type { Directional = 0, Point = 1, Spot = 2 };
  static constexpr int kMaxLights = 4;  // of each type (rendering::kMaxLightsPerType)

  Type GetType() const { return type_; }

  // Alpha is ignored. Values past 1 are fine (or use the intensity).
  void SetColor(const Color& color) { color_ = color; }
  const Color& GetColor() const { return color_; }
  // Brightness multiplier on the colour (diffuse and specular).
  void SetIntensity(float intensity) { intensity_ = intensity; }
  float GetIntensity() const { return intensity_; }
  // How much of the light reaches surfaces that face away from it (and shadowed ones): the
  // "fill" that keeps dark sides from going black.
  void SetAmbient(float ambient) { ambient_ = ambient; }
  float GetAmbient() const { return ambient_; }

  // Points the light's forward axis (-Z) along `direction` (needn't be normalized) by setting the node's local
  // rotation (LookRotation with +Y up; straight up or down works too).
  void SetDirection(const Vec3& direction) { SetRotation(LookRotation(direction)); }
  // The world-space direction the light shines in (includes the parents' rotations).
  Vec3 GetDirection() const { return Forward(); }

  // Casting shadows makes the renderer draw the shadow casters from this light into a shadow
  // map every frame. Directional lights only (for now); one per scene.
  void SetCastsShadows(bool casts) { casts_shadows_ = casts; }
  bool CastsShadows() const { return casts_shadows_; }
  ShadowSettings shadow;

 protected:
  explicit Light3D(Type type) : Node3D(NodeKind::Light), type_(type) {}

 private:
  Type type_;
  Color color_{1.0f, 1.0f, 1.0f};
  float intensity_ = 1.0f;
  float ambient_ = 0.2f;
  bool casts_shadows_ = false;
};

// Parallel rays from far away, like the sun: only the direction matters, not the position.
class DirectionalLight3D : public Light3D {
 public:
  DirectionalLight3D() : Light3D(Type::Directional) { SetDirection({0.0f, -1.0f, 0.0f}); }
};

// A bulb: shines in every direction from the node's position, fading out by `range`.
class PointLight3D : public Light3D {
 public:
  PointLight3D() : Light3D(Type::Point) {}
  void SetRange(float range) { range_ = range; }
  float GetRange() const { return range_; }

 private:
  float range_ = 10.0f;
};

// A cone from the node's position along its direction: full brightness inside the inner angle, fading to nothing at
// the outer angle (half-angles from the axis, in radians).
class SpotLight3D : public Light3D {
 public:
  SpotLight3D() : Light3D(Type::Spot) { SetDirection({0.0f, -1.0f, 0.0f}); }
  void SetRange(float range) { range_ = range; }
  float GetRange() const { return range_; }
  void SetInnerAngle(float radians) { inner_angle_ = radians; }
  float GetInnerAngle() const { return inner_angle_; }
  void SetOuterAngle(float radians) { outer_angle_ = radians; }
  float GetOuterAngle() const { return outer_angle_; }

 private:
  float range_ = 10.0f;
  float inner_angle_ = 12.5f * kDegToRad;
  float outer_angle_ = 17.5f * kDegToRad;
};

#if !WLW_NO_DEPRECATED
using DirectionalLight WLW_DEPRECATED(0.3, "Use DirectionalLight3D") = DirectionalLight3D;
#endif

}  // namespace wlw::scene
