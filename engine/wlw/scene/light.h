#pragma once

#include <cmath>

#include <glm/glm.hpp>

#include "wlw/scene/node.h"

namespace wlw::scene {

// How a light's shadows look (only directional lights cast shadows for now).
struct ShadowSettings {
  int map_size = 1024;    // the shadow map is map_size x map_size depth texels
  float softness = 1.5f;  // edge blur: radius of the PCF filter, in shadow-map texels
  float strength = 0.6f;  // 0 = no darkening, 1 = only the ambient light is left in shadow
};

// A light source is a node: added to the scene with Window::AddNode like geometry, moved and
// turned like any node (and with its parent), hidden with SetVisible(false). It has no model, so
// nothing is drawn for it; the renderer gathers the visible lights every frame and lights the
// scene with them (up to kMaxLights of each type).
//
// Directional and spot lights shine along the node's forward axis, -Z, turned by its rotation.
// SetDirection is the convenient way to aim them.
class LightNode : public Node3D {
 public:
  enum class Type { Directional = 0, Point = 1, Spot = 2 };
  static constexpr int kMaxLights = 4;  // of each type (rendering::kMaxLightsPerType)

  Type GetType() const { return type_; }
  const LightNode* AsLight() const override { return this; }

  void SetColor(const glm::vec3& color) { color_ = color; }
  const glm::vec3& GetColor() const { return color_; }
  // Brightness multiplier on the colour (diffuse and specular).
  void SetIntensity(float intensity) { intensity_ = intensity; }
  float GetIntensity() const { return intensity_; }
  // How much of the light reaches surfaces that face away from it (and shadowed ones): the
  // "fill" that keeps dark sides from going black.
  void SetAmbient(float ambient) { ambient_ = ambient; }
  float GetAmbient() const { return ambient_; }

  // Points the light's forward axis (-Z) along `direction` by setting the node's rotation.
  void SetDirection(const glm::vec3& direction) {
    const glm::vec3 d = glm::normalize(direction);
    // The node rotates by X, then Y, then Z (Node::UpdateLocalMatrix). Z doesn't move the -Z axis;
    // Y turns it to (-sin y, 0, -cos y); X then tilts that to (-sin y, cos y sin x, -cos y cos x).
    const float y = std::asin(glm::clamp(-d.x, -1.0f, 1.0f));
    const float x = std::atan2(d.y, -d.z);
    SetRotation({glm::degrees(x), glm::degrees(y), 0.0f});
  }
  // The world-space direction the light shines in (includes the parents' rotations).
  glm::vec3 GetDirection() const { return glm::normalize(glm::mat3(GetModelMatrix()) * glm::vec3(0.0f, 0.0f, -1.0f)); }

  // Casting shadows makes the renderer draw the shadow casters from this light into a shadow
  // map every frame. Directional lights only (for now); one per scene.
  void SetCastsShadows(bool casts) { casts_shadows_ = casts; }
  bool CastsShadows() const { return casts_shadows_; }
  ShadowSettings shadow;

 protected:
  explicit LightNode(Type type) : type_(type) {}

 private:
  Type type_;
  glm::vec3 color_{1.0f, 1.0f, 1.0f};
  float intensity_ = 1.0f;
  float ambient_ = 0.2f;
  bool casts_shadows_ = false;
};

// Parallel rays from far away, like the sun: only the direction matters, not the position.
class DirectionalLight : public LightNode {
 public:
  DirectionalLight() : LightNode(Type::Directional) { SetDirection({0.0f, -1.0f, 0.0f}); }
};

// A bulb: shines in every direction from the node's position, fading out by `range`.
class PointLight : public LightNode {
 public:
  PointLight() : LightNode(Type::Point) {}
  void SetRange(float range) { range_ = range; }
  float GetRange() const { return range_; }

 private:
  float range_ = 10.0f;
};

// A cone from the node's position along its direction: full brightness inside `inner_degrees`,
// fading to nothing at `outer_degrees` (half-angles).
class SpotLight : public LightNode {
 public:
  SpotLight() : LightNode(Type::Spot) { SetDirection({0.0f, -1.0f, 0.0f}); }
  void SetRange(float range) { range_ = range; }
  float GetRange() const { return range_; }
  void SetCone(float inner_degrees, float outer_degrees) {
    inner_degrees_ = inner_degrees;
    outer_degrees_ = outer_degrees;
  }
  float GetInnerDegrees() const { return inner_degrees_; }
  float GetOuterDegrees() const { return outer_degrees_; }

 private:
  float range_ = 10.0f;
  float inner_degrees_ = 12.5f;
  float outer_degrees_ = 17.5f;
};

}  // namespace wlw::scene
