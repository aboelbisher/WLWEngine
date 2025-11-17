#include <iostream>

#include "root/wlw.h"
#include "scene/window.h"
#include <chrono>
#include <thread>
#include <scene/triangle_2d.h>
#include <core/vertex_2d.h>
#include <array>
#include <core/color.h>
#include <core/mesh.h>
#include <rendering/material.h>

#include <utils/image_loader.h>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include "data_helper.h"
#include <filesystem>
using namespace wlw;

namespace fs = std::filesystem;
int main() {

	auto engine = wlw::WLWEngine::Create();
	std::shared_ptr< wlw::scene::Window> window = wlw::scene::Window::Create({1024, 1024 }, "MainWindow");
	engine->Start(window);
	auto cube = createCubeWithNormals();
	auto cube_node = std::make_shared<scene::Node3D>();
	cube_node->SetMesh(cube);
	window->AddNode(cube_node);

	auto material = rendering::Material::Create();
	auto image = utils::ImageLoader::LoadImage("cube.jpg");
	material->SetTexture(std::move(image));
	auto lighting = rendering::Lighting{
	.position = {1.2f, 1.0f, 2.0f}, .color = core::Color(1.0, 1.0, 1.0, 1.0),
	.ambient_strength = 0.1, .shininess = 64.0f
	};

	material->SetLighting(lighting);

	cube_node->SetMaterial(std::move(material));

	auto pyramid = createPyramidFrustumWithNormals();
	auto pyramid_node = std::make_shared<wlw::scene::Node3D>();
	pyramid_node->SetMesh(pyramid);
	pyramid_node->SetPosition({ 3.0f, 0.0f, 5.0f });
	window->AddNode(pyramid_node);
	


	//glm::vec3 lightPosition = glm::vec3(1.2f, 1.0f, 2.0f);
	//glm::vec3 lightColour = glm::vec3(1.0f, 1.0f, 1.0f); // White light
	//glm::vec3 objectColour = glm::vec3(1.0f, 0.5f, 0.31f); // Orange-red object


	auto pyramid_material = rendering::Material::Create();
	auto pyramid_texture = utils::ImageLoader::LoadImage("pyramid.jpg");
	pyramid_material->SetTexture(std::move(pyramid_texture));
	pyramid_node->SetMaterial(std::move(pyramid_material));


	while(true) {
		engine->Iterate();
	}
}