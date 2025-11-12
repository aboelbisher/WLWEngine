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
#include <utils/gltf_loader.h>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include "data_helper.h"
#include <filesystem>
using namespace wlw;

namespace fs = std::filesystem;
int main() {

	auto engine = wlw::WLWEngine::Create();
	std::shared_ptr< wlw::scene::Window> window = wlw::scene::Window::Create({ 2048, 2048 }, "MainWindow");
	engine->Start(window);

	
	
	auto cube_mesh = createCubeWithNormals();
	auto cube_node = std::make_shared<scene::Node3D>();
	auto cube_model = std::make_shared<core::Model<core::Vertex3D>>();
	cube_model->meshes.push_back(cube_mesh);
	cube_node->SetModel(cube_model);
	cube_node->SetPosition({ 0.0f, 0.0f, 0.0f });

	auto pyramid1_mesh = createPyramidFrustumWithNormals(1.0, 0.5, 1.0f);
	auto pyramid1_node = std::make_shared<scene::Node3D>();
	auto pyramid1_model = std::make_shared<core::Model<core::Vertex3D>>();
	pyramid1_model->meshes.push_back(pyramid1_mesh);
	pyramid1_node->SetModel(pyramid1_model);
	pyramid1_node->SetPosition({ 0.0f, 0.0f, 1.1f });
	cube_node->AddNode(pyramid1_node);


	std::shared_ptr<rendering::Material> material = rendering::Material::Create();
	auto image = utils::ImageLoader::LoadImage("cube.jpg");
	material->SetTexture(std::move(image));
	auto lighting = rendering::Lighting{
	.position = {1.2f, 1.0f, 2.0f},
	.ambient_strength = 0.5, .shininess = 128.0f
	};
	material->SetLighting(lighting);
	material->color = core::Color(1.0, 1.0, 1.0, 1.0);
	cube_node->SetMaterial(material);
	window->AddNode(cube_node);

		

	auto pyramid_mesh = createPyramidFrustumWithNormals();
	auto pyramid_node = std::make_shared<wlw::scene::Node3D>();
	auto pyramid_model = std::make_shared<core::Model<core::Vertex3D>>();
	pyramid_model->meshes.push_back(pyramid_mesh);
	pyramid_node->SetModel(pyramid_model);
	pyramid_node->SetPosition({ 3.0f, 0.0f, 5.0f });
	std::shared_ptr<rendering::Material> pyramid_material = rendering::Material::Create();
	std::shared_ptr<utils::Texture> pyramid_texture = utils::ImageLoader::LoadImage("pyramid.jpg");
	pyramid_material->SetTexture(pyramid_texture);
	pyramid_node->SetMaterial(pyramid_material);
	//window->AddNode(pyramid_node);


	auto sphere_mesh = createSphereWithNormals();
	auto sphere_node = std::make_shared<wlw::scene::Node3D>();
	auto sphere_model = std::make_shared<core::Model<core::Vertex3D>>();
	sphere_model->meshes.push_back(sphere_mesh);
	sphere_node->SetModel(sphere_model);
	sphere_node->SetPosition(lighting.position);  
	sphere_node->SetMaterial(pyramid_material);
	window->AddNode(sphere_node);

	
	auto random_model = utils::GLTFLoader::LoadModel("random_model/scene.gltf");
	auto random_model_node = std::make_shared<scene::Node3D>();
	random_model_node->SetModel(random_model);
	random_model_node->SetPosition({ 0.0f, 5.0f, 0.0f });

	std::shared_ptr<rendering::Material> model_material = rendering::Material::Create();
	auto lighting1 = rendering::Lighting{
	.position = {1.2f, 1.0f, 2.0f},
	.ambient_strength = 0.5, .shininess = 128.0f
	};
	model_material->SetLighting(lighting1);
	model_material->color = core::Color(1.0, 1.0, 1.0, 1.0);
	random_model_node->SetMaterial(model_material);
	window->AddNode(random_model_node);

	auto window_ = window->GetGLFWwindow();
	while(true) {
		core::Vector3 pos_change = {};
		if (glfwGetKey(window_, GLFW_KEY_UP) == GLFW_PRESS) {
			pos_change = {0.00, 0.01, 0.0};
		}
		// Backward
		if (glfwGetKey(window_, GLFW_KEY_DOWN) == GLFW_PRESS) {
			pos_change =  {0.00, -0.01, 0.0};
		}
		// Left (Strafe)
		if (glfwGetKey(window_, GLFW_KEY_LEFT) == GLFW_PRESS) {
			pos_change = {-0.01, 0.0, 0.0};
		}
		// Right (Strafe)
		if (glfwGetKey(window_, GLFW_KEY_RIGHT) == GLFW_PRESS) {
			pos_change = {+0.01, 0.0, 0.0};
		}

		lighting.position += pos_change;
		material->SetLighting(lighting);

		//sphere_node->SetPosition(lighting.position);

		//random_model_node->SetPosition(random_model_node->GetPosition() + pos_change);

		//cube_node->SetPosition(cube_node->GetPosition() + pos_change);

		engine->Iterate();
	}
}