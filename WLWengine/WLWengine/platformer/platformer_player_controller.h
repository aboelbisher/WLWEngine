#pragma once
#include <memory>
#include <vector>
#include "scene/node.h"
#include "core/collision.h"
#include <GLFW/glfw3.h>

namespace wlw::platformer {

    class PlayerController {
    public:
        PlayerController(std::shared_ptr<scene::Node3D> player_node, const std::vector<scene::AABB>& colliders) 
          : player_node_(player_node), static_colliders_(colliders), velocity_y_(0.0f), is_grounded_(false) {}

        void Update(GLFWwindow* window, float delta_time) {
          float speed = 8.0f;
          float jump_force = 20.0f;
          float gravity = -30.0f;

          core::Vector3 current_pos = player_node_->GetPosition();
          core::Vector3 next_pos = current_pos;

          // Horizontal movement (Snappy)
          float move_x = 0.0f;
          if (glfwGetKey(window, GLFW_KEY_LEFT) == GLFW_PRESS)  move_x -= speed * delta_time;
          if (glfwGetKey(window, GLFW_KEY_RIGHT) == GLFW_PRESS) move_x += speed * delta_time;

          // Vertical physics
          velocity_y_ += gravity * delta_time;
          if (is_grounded_ && (glfwGetKey(window, GLFW_KEY_SPACE) == GLFW_PRESS || glfwGetKey(window, GLFW_KEY_UP) == GLFW_PRESS)) {
            velocity_y_ = jump_force;
            is_grounded_ = false;
          }

          float move_y = velocity_y_ * delta_time;

          // Resolve X
          core::Vector3 pos_x = current_pos;
          pos_x.x += move_x;
          scene::AABB aabb_x = scene::AABB::FromPositionAndSize(pos_x, 0.6f);
          bool hit_x = false;
          for (const auto& wall : static_colliders_) {
            if (aabb_x.Intersects(wall)) {
              hit_x = true;
              break;
            }
          }
          if (!hit_x) next_pos.x = pos_x.x;

          // Resolve Y
          core::Vector3 pos_y = next_pos; // Use updated X for Y check
          pos_y.y += move_y;
          scene::AABB aabb_y = scene::AABB::FromPositionAndSize(pos_y, 0.6f);
          bool hit_y = false;
          is_grounded_ = false; // Reset grounded each frame unless hit bottom
          for (const auto& wall : static_colliders_) {
            if (aabb_y.Intersects(wall)) {
              hit_y = true;
              if (velocity_y_ < 0) {
                is_grounded_ = true;
                velocity_y_ = 0;
                // Snap to floor
                next_pos.y = wall.max.y + 0.3f;
              } else if (velocity_y_ > 0) {
                velocity_y_ = 0;
                // Snap to ceiling
                next_pos.y = wall.min.y - 0.3f;
              }
              break;
            }
          }
          if (!hit_y) next_pos.y = pos_y.y;
          player_node_->SetPosition(next_pos);
        }

        scene::AABB GetPlayerAABB() const {
          return scene::AABB::FromPositionAndSize(player_node_->GetPosition(), 0.6f);
        }

    private:
        std::shared_ptr<scene::Node3D> player_node_;
        std::vector<scene::AABB> static_colliders_;
        float velocity_y_;
        bool is_grounded_;
    };
}
