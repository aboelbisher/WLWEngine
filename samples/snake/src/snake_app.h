#pragma once

#include <memory>
#include <vector>

#include "snake_game.h"
#include "wlw/app/application.h"
#include "wlw/rendering/material.h"
#include "wlw/scene/follow_camera.h"
#include "wlw/scene/node.h"
#include "wlw/scene/orthographic_camera.h"

namespace snake {

// The "view + controller" side of the game: turns input into SnakeGame calls, advances the game
// on its tick, and mirrors the game state into scene nodes. All rules live in SnakeGame.
class SnakeApp : public wlw::Application {
 public:
  SnakeApp();

 protected:
  void OnStart() override;
  void OnFixedUpdate(float dt) override;
  void OnUpdate(float dt) override;
  void OnRender(float alpha) override;

 private:
  using Model = std::shared_ptr<wlw::core::Model<wlw::core::Vertex3D>>;
  using NodePtr = std::shared_ptr<wlw::scene::Node3D>;

  void BuildBoard();
  void ReadInput();
  void Restart();
  void SetupCameras();
  void FitPerspectiveCameraToScreen();
  void SyncSnakeNodes(float t);   // game state -> pooled segment nodes
  NodePtr AcquireSegmentNode(size_t index);
  void UpdateTitle();

  static constexpr int kGridSize = 20;

  SnakeGame game_{kGridSize};
  float move_timer_ = 0.0f;   // time accumulated towards the next game step
  bool interpolate_ = true;   // I key toggles: smooth vs cell-to-cell motion
  bool orthographic_ = true;  // P key toggles: flat top-down board vs tilted perspective view
  float food_spin_ = 0.0f;

  std::shared_ptr<wlw::rendering::Material> material_;
  Model head_model_, dead_head_model_, body_model_, food_model_;

  // Segment node pool: nodes are created only when the snake grows longer than ever before,
  // and are hidden (not destroyed) when it gets shorter again (on restart).
  std::vector<NodePtr> segment_nodes_;
  NodePtr food_node_;
  std::shared_ptr<wlw::scene::OrthographicCamera> ortho_camera_;
  std::shared_ptr<wlw::scene::FollowCamera> perspective_camera_;
};

}  // namespace snake
