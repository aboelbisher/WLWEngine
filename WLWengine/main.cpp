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
	std::shared_ptr< wlw::scene::Window> window = wlw::scene::Window::Create({ 1024, 1024 }, "MainWindow");
	engine->Start(window);
	auto cube = createCubeWithNormals();
	auto cube_node = std::make_shared<scene::Node3D>();
	cube_node->SetMesh(cube);
	cube_node->SetPosition({ 0.0f, 0.0f, 0.0f });

	auto pyramid1 = createPyramidFrustumWithNormals(1.0, 0.5, 1.0f);
	auto pyramid1_node = std::make_shared<scene::Node3D>();
	pyramid1_node->SetMesh(pyramid1);
	pyramid1_node->SetPosition({ 0.0f, 0.0f, 1.1f });
	cube_node->AddNode(pyramid1_node);

	window->AddNode(cube_node);


	std::shared_ptr<rendering::Material> material = rendering::Material::Create();
	auto image = utils::ImageLoader::LoadImage("cube.jpg");
	material->SetTexture(std::move(image));
	auto lighting = rendering::Lighting{
	.position = {1.2f, 1.0f, 2.0f}, .color = core::Color(1.0, 1.0, 1.0, 1.0),
	.ambient_strength = 0.5, .shininess = 128.0f
	};
	material->SetLighting(lighting);
	cube_node->SetMaterial(material);


	auto pyramid = createPyramidFrustumWithNormals();
	auto pyramid_node = std::make_shared<wlw::scene::Node3D>();
	pyramid_node->SetMesh(pyramid);
	pyramid_node->SetPosition({ 3.0f, 0.0f, 5.0f });
	window->AddNode(pyramid_node);
	std::shared_ptr<rendering::Material> pyramid_material = rendering::Material::Create();
	auto pyramid_texture = utils::ImageLoader::LoadImage("pyramid.jpg");
	pyramid_material->SetTexture(std::move(pyramid_texture));
	pyramid_node->SetMaterial(std::move(pyramid_material));


	auto sphere = createSphereWithNormals();
	auto sphere_node = std::make_shared<wlw::scene::Node3D>();
	sphere_node->SetMesh(sphere);
	sphere_node->SetPosition(lighting.position);
	window->AddNode(sphere_node);

	auto window_ = window->GetGLFWwindow();
	while(true) {
		if (glfwGetKey(window_, GLFW_KEY_UP) == GLFW_PRESS) {
			lighting.position += {0.00, 0.01, 0.0};
		}
		// Backward
		if (glfwGetKey(window_, GLFW_KEY_DOWN) == GLFW_PRESS) {
			lighting.position -= {0.00, 0.01, 0.0};
		}
		// Left (Strafe)
		if (glfwGetKey(window_, GLFW_KEY_LEFT) == GLFW_PRESS) {
			lighting.position -= {0.01, 0.0, 0.0};
		}
		// Right (Strafe)
		if (glfwGetKey(window_, GLFW_KEY_RIGHT) == GLFW_PRESS) {
			lighting.position += {0.01, 0.0, 0.0};
		}

		std::cout << "position " << lighting.position.x << std::endl;
		material->SetLighting(lighting);

		cube_node->SetPosition(lighting.position);

		//cube_node->SetPosition(cube_node->GetPosition() + lighting.position);

		engine->Iterate();
	}
}