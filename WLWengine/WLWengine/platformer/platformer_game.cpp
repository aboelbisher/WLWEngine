#include "platformer_game.h"
#include "platformer_player_controller.h"
#include "platformer_level.h"
#include "scene/follow_camera.h"
#include <iostream>
#include <vector>
#include <string>
#include <GLFW/glfw3.h>

namespace wlw::platformer {

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
      current_level_idx_ = index % 2;
      window_->ClearScene3D();
      level_data_.collectibles.clear();
      level_data_.static_colliders.clear();

      Level level_loader;
      std::vector<std::string> map;
      auto light = std::make_shared<rendering::Lighting>();

      if (current_level_idx_ == 0) {
        map = {
          "FFFFFFFFFFFFFFFFFFFF",
          "FFFFFFFFFFFFFFFFFFFF",
          "FBBBBBBBBBBBBBBBBBBF",
          "FB................BF",
          "FB................BF",
          "FB...####.........BF",
          "FB.......##.......BF",
          "FB.........##...C.BF",
          "FB..P.......#######F",
          "FB.###............BF",
          "FBBBBBBBBBBBBBBBBBBF",
          "####################"
        };
        light->type = rendering::LightType::Directional;
        light->direction = {-0.5f, -0.5f, -1.0f};
        light->ambient_strength = 0.3f;
        light->color = {1.0f, 1.0f, 0.9f, 1.0f};
      } else {
         map = {
          "FFFFFFFFFFFFFFFFFFFF",
          "F..................F",
          "F...C..............F",
          "F..####............F",
          "F......##..........F",
          "F........##........F",
          "F..........##......F",
          "F..P.........##..C.F",
          "F.####........#####F",
          "F..................F",
          "FFFFFFFFFFFFFFFFFFFF",
          "####################"
        };
        light->type = rendering::LightType::Point;
        light->position = {10.0f, 5.0f, 5.0f};
        light->ambient_strength = 0.1f;
        light->color = {0.7f, 0.7f, 1.0f, 1.0f};
      }

      current_light_ = light;
      auto driver = engine_->GetRenderingDriver();
      auto device = engine_->GetRenderDevice();
      level_data_ = level_loader.Load(map, window_, light, device);

      // --- ADD SKYBOX ---
      std::array<std::string, 6> faces = { "cube.jpg", "cube.jpg", "cube.jpg", "cube.jpg", "cube.jpg", "cube.jpg" };
      std::array<utils::RawImage, 6> raw_faces;
      for (int i = 0; i < 6; ++i) {
          auto img = utils::ImageLoader::LoadImage(faces[i]);
          if (img) raw_faces[i] = std::move(*img);
      }
      auto cubemap = device->CreateCubemap(raw_faces);
      window_->SetSkybox(cubemap);
      
      if (level_data_.player_node) {
        player_controller_ = std::make_unique<PlayerController>(level_data_.player_node, level_data_.static_colliders);
        auto follow_cam = scene::FollowCamera::Create(level_data_.player_node);
        follow_cam->SetOffset({0.0f, 2.0f, 12.0f}); 
        camera_ = std::move(follow_cam);
        window_->SetCamera(camera_);
      }
    }

    void Update(float delta_time) override {
      auto glfw_win = window_->GetGLFWwindow();
      if (!glfw_win) return;

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
        player_controller_->Update(glfw_win, delta_time);
        
        // Rotate the player node (GLTF model) continuously
        if (level_data_.player_node) {
            auto rot = level_data_.player_node->GetRotation();
            rot.y += 90.0f * delta_time; 
            level_data_.player_node->SetRotation(rot);
        }

        scene::AABB player_aabb = player_controller_->GetPlayerAABB();
        for (auto it = level_data_.collectibles.begin(); it != level_data_.collectibles.end(); ) {
          scene::AABB coll_aabb = scene::AABB::FromPositionAndSize(it->second->GetPosition(), 0.5f);
          if (player_aabb.Intersects(coll_aabb)) {
            window_->RemoveNode3D(it->first);
            it = level_data_.collectibles.erase(it);
          } else {
            auto rot = it->second->GetRotation();
            rot.y += 100.0f * delta_time;
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
      float last_time = (float)glfwGetTime();
      while (!glfwWindowShouldClose(window_->GetGLFWwindow())) {
        float current_time = (float)glfwGetTime();
        float dt = current_time - last_time;
        if (dt > 0.1f) dt = 0.1f; // Cap dt for stability
        last_time = current_time;
        Update(dt);
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
    core::Vector2 window_size_;
    std::string window_title_;
  };

  std::unique_ptr<Game> Game::Create(const core::Vector2& size, const std::string& title) {
    return std::make_unique<GameImpl>(size, title);
  }
}
