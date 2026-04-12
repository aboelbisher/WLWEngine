#pragma once
#include <memory>
#include <vector>
#include <iostream>
#include "../root/scene/node.h"
#include "core/collision.h"
#include <GLFW/glfw3.h>

namespace wlw::game {
    class PlayerController {
    public:
        PlayerController(std::shared_ptr<scene::Node3D> player_node, const std::vector<scene::AABB>& colliders) 
          : player_node_(player_node), static_colliders_(colliders) {}

        void Update(GLFWwindow* window) {
          core::Vector3 move_dir = {0, 0, 0};
          float speed = 0.05f;

          if (glfwGetKey(window, GLFW_KEY_UP) == GLFW_PRESS)    move_dir.z -= speed;
          if (glfwGetKey(window, GLFW_KEY_DOWN) == GLFW_PRESS)  move_dir.z += speed;
          if (glfwGetKey(window, GLFW_KEY_LEFT) == GLFW_PRESS)  move_dir.x -= speed;
          if (glfwGetKey(window, GLFW_KEY_RIGHT) == GLFW_PRESS) move_dir.x += speed;

          if (move_dir.x != 0 || move_dir.z != 0) {
            core::Vector3 next_pos = player_node_->GetPosition() + move_dir;
            
            // Basic AABB check for player (approx size 0.5)
            scene::AABB player_aabb = scene::AABB::FromPositionAndSize(next_pos, 0.5f);
            
            for (const auto& wall : static_colliders_) {
              if (player_aabb.Intersects(wall)) {
                return;
              }
            }

             player_node_->SetPosition(next_pos);
            
          }
        }

        scene::AABB GetPlayerAABB() const {
          return scene::AABB::FromPositionAndSize(player_node_->GetPosition(), 0.5f);
        }

    private:
        std::shared_ptr<scene::Node3D> player_node_;
        std::vector<scene::AABB> static_colliders_;
    };
}
