#include "level.h"
#include "data_helper.h"
#include "../root/utils/gltf_loader.h"
#include "../root/rendering/material.h"
#include <map>

namespace wlw::game {

LevelResult Level::Load(const std::vector<std::string>& map_data, std::shared_ptr<scene::Window> window, std::shared_ptr<rendering::Lighting> light) {
  LevelResult result;
  result.player_start_pos = {0.0f, 0.0f, 0.0f};

  // Materials
  std::shared_ptr<rendering::Material> wall_material = rendering::Material::Create();
  wall_material->SetLighting(*light);
  core::Color wall_color = {0.3f, 0.3f, 0.3f, 1.0f};

  std::shared_ptr<rendering::Material> floor_material = rendering::Material::Create();
  floor_material->SetLighting(*light);
  core::Color floor_color_1 = {0.4f, 0.4f, 0.4f, 1.0f};
  core::Color floor_color_2 = {0.5f, 0.5f, 0.5f, 1.0f};

  std::shared_ptr<rendering::Material> collectible_material = rendering::Material::Create();
  collectible_material->SetLighting(*light);
  core::Color collectible_color = {1.0f, 0.8f, 0.0f, 1.0f}; // Gold

  // Meshes & Models
  auto wall_mesh = createCubeWithNormals(1.0f, wall_color);
  auto floor_mesh_1 = createCubeWithNormals(1.0f, floor_color_1);
  auto floor_mesh_2 = createCubeWithNormals(1.0f, floor_color_2);
  auto sphere_mesh = createSphereWithNormals(0.3f); 

  auto wall_model = std::make_shared<core::Model<core::Vertex3D>>();
  wall_model->meshes.push_back(wall_mesh);

  auto floor_model_1 = std::make_shared<core::Model<core::Vertex3D>>();
  floor_model_1->meshes.push_back(floor_mesh_1);

  auto floor_model_2 = std::make_shared<core::Model<core::Vertex3D>>();
  floor_model_2->meshes.push_back(floor_mesh_2);

  auto collectible_mesh = createGemWithNormals(0.4f, 0.6f); 
  auto collectible_model = std::make_shared<core::Model<core::Vertex3D>>();
  collectible_model->meshes.push_back(collectible_mesh);

  // Parsing
  for (int z = 0; z < map_data.size(); ++z) {
    for (int x = 0; x < map_data[z].size(); ++x) {
      char tile = map_data[z][x];
      core::Vector3 pos = {(float)x, 0.0f, (float)z};

      if (tile != '#') {
        auto floor_node = std::make_shared<scene::Node3D>();
        floor_node->SetModel((x + z) % 2 == 0 ? floor_model_1 : floor_model_2);
        floor_node->SetMaterial(floor_material);
        floor_node->SetScale({1.0f, 0.1f, 1.0f});
        floor_node->SetPosition({pos.x, -0.05f, pos.z});
        window->AddNode(floor_node);
      }

      if (tile == '#') {
        auto wall_node = std::make_shared<scene::Node3D>();
        wall_node->SetModel(wall_model);
        wall_node->SetMaterial(wall_material);
        wall_node->SetScale({1.0f, 1.0f, 1.0f});
        wall_node->SetPosition({pos.x, 0.5f, pos.z});
        window->AddNode(wall_node);
        result.static_colliders.push_back(scene::AABB::FromPositionAndScale(wall_node->GetPosition(), wall_node->GetScale()));
      } else if (tile == 'P') {
        result.player_start_pos = {pos.x, 0.5f, pos.z};
        auto player_mesh = createCubeWithNormals(0.6f, {0.0f, 0.4f, 0.8f, 1.0f}); // Blue cube
        auto player_model = std::make_shared<core::Model<core::Vertex3D>>();
        player_model->meshes.push_back(player_mesh);

        result.player_node = std::make_shared<scene::Node3D>();
        result.player_node->SetModel(player_model);
        
        std::shared_ptr<rendering::Material> player_material = rendering::Material::Create();
        player_material->SetLighting(*light);
        result.player_node->SetMaterial(player_material);
        
        result.player_node->SetPosition(result.player_start_pos);
        result.player_node->SetScale({1.0f, 1.0f, 1.0f}); 
        window->AddNode(result.player_node);
      }
 else if (tile == 'C') {
        auto coll_node = std::make_shared<scene::Node3D>();
        coll_node->SetModel(collectible_model);
        coll_node->SetMaterial(collectible_material);
        coll_node->SetPosition({pos.x, 0.5f, pos.z});
        int id = window->AddNode(coll_node);
        result.collectibles[id] = coll_node;
      }
    }
  }

  return result;
}

} // namespace wlw::game
