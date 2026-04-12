#include "game.h"
#include "player_controller.h"
#include "level.h"
#include "../root/scene/follow_camera.h"
#include <iostream>
#include <vector>
#include <string>

namespace wlw::game {
  class GameImpl : public Game {
  public:
    GameImpl(const core::Vector2& size, const std::string& title)
      : window_size_(size), window_title_(title) {
      engine_ = wlw::WLWEngine::Create();
      window_ = wlw::scene::Window::Create(size, title);
    }

    void Initialize() override {
      engine_->Start(window_);
      SwitchLevel(0);
    }

    void SwitchLevel(int index) {
      current_level_idx_ = index % 3;
      window_->ClearScene3D();
      level_data_.collectibles.clear();
      level_data_.static_colliders.clear();

      Level level_loader;
      std::vector<std::string> map;
      auto light = std::make_shared<rendering::Lighting>();

      if (current_level_idx_ == 0) {
        std::cout << "Switching to Level 0: Sunlit Arena (Directional Light)\n";
        map = {
          "##########",
          "#P.......#",
          "#........#",
          "#...C....#",
          "#........#",
          "#....C...#",
          "#........#",
          "#........#",
          "#.......C#",
          "##########"
        };
        light->type = rendering::LightType::Directional;
        light->direction = {-0.5f, -1.0f, -0.5f};
        light->ambient_strength = 0.2f;
        light->color = {1.0f, 0.95f, 0.8f, 1.0f}; // Warm sun
      } else if (current_level_idx_ == 1) {
        std::cout << "Switching to Level 1: Dungeon (Point Light)\n";
        map = {
          "##########",
          "#P#......#",
          "#.#.####.#",
          "#.#.C..#.#",
          "#.####.#.#",
          "#....#.#.#",
          "####.#.#.#",
          "#C...#...#",
          "#..#######",
          "##########"
        };
        light->type = rendering::LightType::Point;
        light->position = {5.0f, 2.0f, 5.0f};
        light->ambient_strength = 0.05f;
        light->constant = 1.0f;
        light->linear = 0.09f;
        light->quadratic = 0.032f;
        light->color = {0.8f, 0.8f, 1.0f, 1.0f}; // Cool blue light
      } else {
        std::cout << "Switching to Level 2: Spotlight Stage (Spot Light)\n";
        map = {
          "..........",
          "..........",
          "....C.....",
          "..........",
          "....P.....",
          "..........",
          "..........",
          "....C.....",
          "..........",
          ".........."
        };
        light->type = rendering::LightType::Spot;
        light->position = {5.0f, 5.0f, 5.0f};
        light->direction = {0.0f, -1.0f, 0.0f};
        light->cutOff = std::cos(glm::radians(12.5f));
        light->outerCutOff = std::cos(glm::radians(17.5f));
        light->ambient_strength = 0.0f;
        light->color = {1.0f, 1.0f, 1.0f, 1.0f};
      }

      current_light_ = light;
      level_data_ = level_loader.Load(map, window_, light);
      
      if (level_data_.player_node) {
        player_controller_ = std::make_unique<PlayerController>(level_data_.player_node, level_data_.static_colliders);
        auto follow_cam = scene::FollowCamera::Create(level_data_.player_node);
        follow_cam->SetOffset({0.0f, 8.0f, 4.0f});
        camera_ = std::move(follow_cam);
        window_->SetCamera(camera_);
      }
    }

    void Update(float delta_time) override {
      auto glfw_win = window_->GetGLFWwindow();
      if (!glfw_win) return;

      // Switch level with 'L' key
      static bool l_pressed = false;
      if (glfwGetKey(glfw_win, GLFW_KEY_L) == GLFW_PRESS) {
        if (!l_pressed) {
          SwitchLevel(current_level_idx_ + 1);
          l_pressed = true;
        }
      } else {
        l_pressed = false;
      }

      if (player_controller_) {
        player_controller_->Update(glfw_win);
        
        // If it's a spot light, make it follow the player like a searchlight
        if (current_light_ && current_light_->type == rendering::LightType::Spot) {
          current_light_->position = level_data_.player_node->GetPosition() + core::Vector3{0, 5, 0};
        }

        scene::AABB player_aabb = player_controller_->GetPlayerAABB();
        for (auto it = level_data_.collectibles.begin(); it != level_data_.collectibles.end(); ) {
          scene::AABB coll_aabb = scene::AABB::FromPositionAndSize(it->second->GetPosition(), 0.5f);
          if (player_aabb.Intersects(coll_aabb)) {
            std::cout << "Collected! ID: " << it->first << "\n";
            window_->RemoveNode3D(it->first);
            it = level_data_.collectibles.erase(it);
            score_++;
          } else {
            auto rot = it->second->GetRotation();
            rot.y += 2.0f; // Spin faster
            it->second->SetRotation(rot);
            ++it;
          }
        }
      }
    }

    void Render() override {
      engine_->Iterate();
    }

    void Run() override {
      Initialize();
      while (true) {
        Update(0.016f);
        Render();
      }
    }

  private:
    std::shared_ptr<wlw::scene::Window> window_;
    std::unique_ptr<wlw::WLWEngine> engine_;
    std::unique_ptr<PlayerController> player_controller_;
    std::shared_ptr<scene::Camera3D> camera_;
    std::shared_ptr<rendering::Lighting> current_light_;
    LevelResult level_data_;
    int current_level_idx_ = 0;
    int score_ = 0;
    core::Vector2 window_size_;
    std::string window_title_;
  };

  std::unique_ptr<Game> Game::Create(const core::Vector2& size, const std::string& title) {
    return std::make_unique<GameImpl>(size, title);
  }
}
