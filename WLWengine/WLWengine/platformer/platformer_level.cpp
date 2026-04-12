#include "platformer_level.h"
#include "../game/data_helper.h"
#include "rendering/material.h"
#include <utils/image_loader.h>
#include <utils/gltf_loader.h>
#include <utils/texture_manager.h>

namespace wlw::platformer {

LevelResult Level::Load(const std::vector<std::string>& map_data, std::shared_ptr<scene::Window> window, std::shared_ptr<rendering::Lighting> light, rendering::RenderDevice* device) {
  LevelResult result;
  result.player_start_pos = {0.0f, 0.0f, 0.0f};

  auto& tex_manager = utils::TextureManager::GetInstance();

  // Materials
  std::shared_ptr<rendering::Material> wall_material = rendering::Material::Create();
  wall_material->SetLighting(*light);
  core::Color wall_color = {0.4f, 0.4f, 0.45f, 1.0f};

  std::shared_ptr<rendering::Material> bg_material = rendering::Material::Create();
  bg_material->SetLighting(*light);
  core::Color bg_color = {0.2f, 0.2f, 0.25f, 1.0f};

  std::shared_ptr<rendering::Material> far_bg_material = rendering::Material::Create();
  far_bg_material->SetLighting(*light);
  core::Color far_bg_color = {0.1f, 0.1f, 0.15f, 1.0f};

  std::shared_ptr<rendering::Material> collectible_material = rendering::Material::Create();
  collectible_material->SetLighting(*light);
  core::Color collectible_color = {1.0f, 0.8f, 0.0f, 1.0f};

  // Meshes
  auto wall_mesh = createCubeWithNormals(1.0f, wall_color);
  auto bg_mesh = createCubeWithNormals(1.0f, bg_color);
  auto far_bg_mesh = createCubeWithNormals(1.0f, far_bg_color);
  auto collectible_mesh = createGemWithNormals(0.4f, 0.6f);

  auto wall_model = std::make_shared<core::Model<core::Vertex3D>>();
  wall_model->meshes.push_back(wall_mesh);

  auto bg_model = std::make_shared<core::Model<core::Vertex3D>>();
  bg_model->meshes.push_back(bg_mesh);

  auto far_bg_model = std::make_shared<core::Model<core::Vertex3D>>();
  far_bg_model->meshes.push_back(far_bg_mesh);

  auto collectible_model = std::make_shared<core::Model<core::Vertex3D>>();
  collectible_model->meshes.push_back(collectible_mesh);

  int height = (int)map_data.size();
  for (int y = 0; y < height; ++y) {
    int row = height - 1 - y; // Bottom-to-top parsing
    for (int x = 0; x < (int)map_data[row].size(); ++x) {
      char tile = map_data[row][x];
      core::Vector3 pos = {(float)x, (float)y, 0.0f};

      if (tile == '#') {
        auto node = std::make_shared<scene::Node3D>();
        node->SetModel(wall_model);
        node->SetMaterial(wall_material);
        node->SetPosition(pos);
        window->AddNode(node);
        result.static_colliders.push_back(scene::AABB::FromPositionAndScale(pos, {1.0f, 1.0f, 1.0f}));
      } else if (tile == 'B') {
        auto node = std::make_shared<scene::Node3D>();
        node->SetModel(bg_model);
        node->SetMaterial(bg_material);
        node->SetPosition({pos.x, pos.y, -5.0f});
        window->AddNode(node);
      } else if (tile == 'F') {
        auto node = std::make_shared<scene::Node3D>();
        node->SetModel(far_bg_model);
        node->SetMaterial(far_bg_material);
        node->SetPosition({pos.x, pos.y, -15.0f});
        window->AddNode(node);
      } else if (tile == 'P') {
        auto random_model = utils::GLTFLoader::LoadModel("random_model/scene.gltf", device);
        auto random_model_node = std::make_shared<scene::Node3D>();
        random_model_node->SetModel(random_model);
        random_model_node->SetPosition(pos);
        auto player_scale = 5.0f;
        random_model_node->SetScale({player_scale, player_scale, player_scale});

        std::shared_ptr<rendering::Material> player_mat = rendering::Material::Create();
        player_mat->SetLighting(*light);
        random_model_node->SetMaterial(player_mat);

        result.player_node = random_model_node;
        window->AddNode(result.player_node);
      } else if (tile == 'C') {
        auto node = std::make_shared<scene::Node3D>();
        node->SetModel(collectible_model);
        node->SetMaterial(collectible_material);
        node->SetPosition(pos);
        int id = window->AddNode(node);
        result.collectibles[id] = node;
      }
    }
  }
  return result;
}

} // namespace wlw::platformer
