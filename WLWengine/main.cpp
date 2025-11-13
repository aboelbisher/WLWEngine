#include <iostream>

#include "root/wlw.h"
#include "root/core/window.h"
#include <chrono>
#include <thread>
#include <scene/triangle_2d.h>

using namespace wlw::core;

int main() {
	auto engine = wlw::WLWEngine::Create();
	std::shared_ptr< wlw::core::Window> window = wlw::core::Window::Create(800, 600, "WLW Engine Window");
	auto shape = std::make_shared<wlw::scene::Triangle2D>();
	window->AddNode(shape);

	//window->SetClearColor(wlw::core::Color(0.0f, 0.0f, 1.0f, 1.0f));
	engine->Start(window);


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