#include <iostream>

#include "root/wlw.h"
#include "root/core/window.h"
#include <chrono>
#include <thread>
#include <scene/triangle_2d.h>
#include <core/vertex_2d.h>
#include <array>
#include <core/color.h>

using namespace wlw;


int main() {
	auto engine = wlw::WLWEngine::Create();
	std::shared_ptr< wlw::core::Window> window = wlw::core::Window::Create(800, 600, "MainWindow");

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
	auto shape = std::make_shared<wlw::scene::Triangle2D>(vertices);
	window->AddNode(shape);


	//window->SetClearColor(wlw::core::Color(0.0f, 0.0f, 1.0f, 1.0f));
	engine->Start(window);


	std::shared_ptr< wlw::core::Window> window1 = wlw::core::Window::Create(1024, 1024, "SecondaryWIdow");

	std::array<wlw::core::Vertex2D, 3> vertices1 = { {

{ { 0.1f, 0.5f }, { 0.0f, 0.0f, 1.0f, 1.0f } },
// V4: Top Right of second triangle
{ { 0.9f,  0.5f}, {0.0f, 0.0f, 1.0f, 1.0f} },
// V5: Bottom Center of second triangle
	{ { 0.5f, -0.0f}, {0.0f, 0.0f, 1.0f, 1.0f} }
} };
	auto shape1 = std::make_shared<wlw::scene::Triangle2D>(vertices1);
	window1->AddNode(shape1);
	engine->AttachWindow(window1);

	while(true) {

		engine->Iterate();
	}
	//	auto window = std::make_shared<wlw::core::Window>(wlw::core::Window::Create(800, 600, "WLW Engine Window"));
	//	window->SetClearColor(wlw::core::Color(0.0f, 1.0f, 0.0f, 1.0f));
	//	engine->Start(window);

	//	std::cout << "Starting 1.5 second nap...\n";
	//	// Using an explicit duration of 1500 milliseconds (1.5 seconds)
	//	std::this_thread::sleep_for(std::chrono::milliseconds(1500));
	//	std::cout << "Woke up!\n";
	//	
	//	engine->Iterate();
	//}	
}