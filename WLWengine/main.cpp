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


  auto frustum = createCubeWithNormals();
  // 2. Rotate the shape 90 degrees around the Y-axis
  //rotateMeshData(frustum, 90.0f, { 0.0f, 1.0f, 0.0f });


	auto shape = std::make_shared<wlw::scene::Node<wlw::core::Vertex3D>>();
  shape->SetMesh(frustum);
	//shape->SetPosition({ 1.0f, -1.0, 0.0f });
	//shape->SetScale({ 0.5f, 0.5f, 0.5f });
	//shape->SetRotation({ 20.0f, 45.0f, 0.0f });
  window->AddNode(shape);

	core::Color red(1.0f, 0.0f, 0.0f, 1.0f);
	std::array<wlw::core::Vertex2D, 3> vertices = { {
			// --- Triangle 1: RED ---
			// V0: Top Left of first triangle
			{{-0.9f,  0.5f}, {1.0f, 0.0f, 0.0f, 1.0f}},
			// V1: Top Right of first triangle
			{{-0.1f,  0.5f}, {1.0f, 0.0f, 0.0f, 1.0f}},
			// V2: Bottom Center of first triangle
			{{-0.5f, -0.0f}, {1.0f, 0.0f, 0.0f, 1.0f}}
		} };
	auto triangle = std::make_shared<wlw::scene::Triangle2D>(vertices);
	window->AddNode(triangle);


	engine->Start(window);

	while(true) {

		engine->Iterate();
	}


}