#include <iostream>

#include "root/wlw.h"
#include "root/core/window.h"
#include <chrono>
#include <thread>
#include <scene/triangle_2d.h>
#include <core/vertex_2d.h>
#include <array>
#include <core/color.h>
#include <core/mesh.h>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include "data_helper.h"

using namespace wlw;


int main() {
	auto engine = wlw::WLWEngine::Create();
	std::shared_ptr< wlw::core::Window> window = wlw::core::Window::Create({800, 600}, "MainWindow");
	engine->Start(window);
	auto cube = createCubeWithNormals();
	auto cube_node = std::make_shared<wlw::scene::Node<wlw::core::Vertex3D>>();
	cube_node->SetMesh(cube);
	window->AddNode(cube_node);

	auto pyramid = createPyramidFrustumWithNormals();
	auto pyramid_node = std::make_shared<wlw::scene::Node<wlw::core::Vertex3D>>();
	pyramid_node->SetMesh(pyramid);
	pyramid_node->SetPosition({ 3.0f, 0.0f, 0.0f });
	window->AddNode(pyramid_node);

	while(true) {
		engine->Iterate();
	}
}