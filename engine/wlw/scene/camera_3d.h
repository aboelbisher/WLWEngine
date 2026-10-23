#pragma once

#include <cstdint>
#include <memory>

#include "wlw/core/geometry.h"
#include "wlw/core/math.h"
#include "wlw/scene/node.h"

namespace wlw::scene {

// A view into the scene: a node, so it is placed, turned and parented like any other (put it under a car and it
// rides along). It looks down its forward axis (-Z) with +Y up on screen; the view matrix is the inverse of its world
// transform with the scale taken out (like Godot's Camera3D). A Viewport draws through one (Viewport::SetCamera); it
// needn't be in the scene's tree (a free camera's world transform is its local one).
//
//   auto camera = scene::Camera3D::Create();
//   camera->SetPosition({0.0f, 5.0f, 10.0f});
//   camera->LookAt({0.0f, 0.0f, 0.0f});
//   window->GetScreen().SetCamera(camera);
//
// Two projections: perspective (SetPerspective: a vertical field of view, things shrink with distance) and
// orthographic (SetOrthographic: a size in world units, parallel rays; top-down, side-on and isometric games, editor
// views).
//
// The aspect ratio (width / height) is never stored: every query takes it from what it is given (the viewport's size,
// a point's screen size), so a camera answers correctly before the first frame and for any viewport it is used by.
//
// What stays in view when the screen's shape changes is the fit mode, applied to the FRAME: a rectangle `size` tall
// (orthographic; perspective: what the vertical field of view covers) and frame_aspect times as wide.
//   KeepHeight (default, like Unity and Godot): the frame's height is shown; a wider screen sees more at the sides.
//   KeepWidth: the frame's width is shown; a taller screen sees more above and below (Godot's keep_width).
//   Contain: the whole frame is always visible, extra space on the longer side (a puzzle board on any phone).
//   Cover: the frame fills the screen, the rest cropped.
// SetFrameAspect(9.0f / 16.0f) + Contain keeps a portrait design framed when a phone turns sideways.
class Camera3D : public Node3D {
 public:
  enum class Projection : uint8_t { Perspective, Orthographic };
  enum class FitMode : uint8_t { KeepHeight, KeepWidth, Contain, Cover };

  // At the origin looking down -Z: perspective, 60 degrees vertically, 0.1 to 1000 m.
  Camera3D() : Node3D(NodeKind::Camera) {}
  static std::shared_ptr<Camera3D> Create() { return std::make_shared<Camera3D>(); }

  // ---- Projection ----
  // `fov_y`: the frame's full vertical angle in radians. Near and far: distances along the view direction (> 0).
  void SetPerspective(float fov_y, float near_plane, float far_plane) {
    projection_ = Projection::Perspective;
    fov_y_ = fov_y;
    SetClipPlanes(near_plane, far_plane);
  }
  // `size`: the frame's height in world units. Depth is linear, so a large near-far range costs little precision.
  void SetOrthographic(float size, float near_plane, float far_plane) {
    projection_ = Projection::Orthographic;
    size_ = size;
    SetClipPlanes(near_plane, far_plane);
  }
  Projection GetProjection() const { return projection_; }
  bool IsOrthographic() const { return projection_ == Projection::Orthographic; }

  void SetFov(float fov_y) { fov_y_ = fov_y; }  // radians (perspective)
  float GetFov() const { return fov_y_; }
  void SetSize(float size) { size_ = size; }  // world units (orthographic)
  float GetSize() const { return size_; }
  void SetClipPlanes(float near_plane, float far_plane) {
    near_ = near_plane;
    far_ = far_plane;
  }
  float GetNear() const { return near_; }
  float GetFar() const { return far_; }

  void SetFitMode(FitMode mode) { fit_mode_ = mode; }
  FitMode GetFitMode() const { return fit_mode_; }
  // The frame's width / height (default 1). Matters for KeepWidth, Contain and Cover.
  void SetFrameAspect(float aspect) { frame_aspect_ = aspect; }
  float GetFrameAspect() const { return frame_aspect_; }

  // The render layers this camera draws (Node3D::SetLayers): a node is drawn when it shares a bit with the mask.
  // Default: every layer. A minimap camera can leave out the player's effects, a UI-model camera see only its own.
  void SetCullMask(uint32_t mask) { cull_mask_ = mask; }
  uint32_t GetCullMask() const { return cull_mask_; }

  // ---- Matrices (aspect = the viewport's width / height) ----
  // World -> camera space: the inverse of the world transform made orthonormal (a scaled parent doesn't
  // squash the picture).
  Mat4 GetViewMatrix() const;
  Mat4 GetProjectionMatrix(float aspect) const;
  Mat4 GetViewProjectionMatrix(float aspect) const { return GetProjectionMatrix(aspect) * GetViewMatrix(); }
  // The world-space volume it sees (for culling, or "is this on screen").
  wlw::Frustum GetFrustum(float aspect) const { return wlw::Frustum::FromMatrix(GetViewProjectionMatrix(aspect)); }

  // How much it sees in world units (width, height) at `distance` in front of it (orthographic: at any distance).
  // For layout: how big a board can be, where the screen's edge is in the world.
  Vec2 VisibleSize(float aspect, float distance = 1.0f) const;

  // ---- Picking ----
  // The world-space ray "under" a screen point: every world point on it is drawn at that point. Raycast it against
  // something (a Plane, an AABB...) to find what the finger/cursor is over. `screen_size` must be in the units of
  // `screen_point` (input::Input::PointerPosition() and SurfaceSize()); the aspect comes from it. Perspective rays
  // fan out from the eye, orthographic rays are parallel; both start on the near plane.
  wlw::Ray ScreenPointToRay(const Vec2& screen_point, const Vec2& screen_size) const;

  // The other direction: where a world point is drawn on screen (what the GPU does to every vertex). x, y in the
  // units of `screen_size`, origin top-left; z = depth in NDC (-1 near, +1 far; outside that range the point is
  // clipped). For UI that follows 3D objects.
  Vec3 WorldToScreen(const Vec3& world, const Vec2& screen_size) const;

 private:
  // Half the visible height after the fit mode: world units (orthographic) or tan(half the vertical angle).
  float HalfHeight(float aspect) const;

  Projection projection_ = Projection::Perspective;
  FitMode fit_mode_ = FitMode::KeepHeight;
  float fov_y_ = 60.0f * kDegToRad;
  float size_ = 10.0f;
  float near_ = 0.1f;
  float far_ = 1000.0f;
  float frame_aspect_ = 1.0f;
  uint32_t cull_mask_ = 0xFFFFFFFFu;
};

}  // namespace wlw::scene
