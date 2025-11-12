#include "window.h"
#include <GLFW/glfw3.h>

#include <iostream>

namespace wlw::core {

#define ASSERT_GLFW_WINDOW_NOT_NULL(window) \
		if (window == NULL) { \
				std::cerr << "GLFW window is NULL" << std::endl; \
				return ; \
		}

class WindowImpl: public Window {
public:
	WindowImpl(int width, int height, const std::string& title): width_(width), height_(height), title_(title), color_(1.0, 0.0, 0.0, 1.0) {}
	~WindowImpl() override {
		// Destructor implementation (e.g., destroy the window)
	}

	void Initialize() override {
		if (window_ != NULL) {
			std::cerr << "GLFW window is already initialized" << std::endl;
			return;
		}
		window_ = glfwCreateWindow(width_, height_, title_.c_str(), NULL, NULL);
		if (window_ == NULL) {
			std::cerr << "Failed to create GLFW window with title " << title_ << std::endl;
			glfwTerminate();
		}
	}


	void MakeContextCurrent() override {
		ASSERT_GLFW_WINDOW_NOT_NULL(window_);
		glfwMakeContextCurrent(window_);
		glViewport(0, 0, width_, height_);
	}

	void ProcessEvents() override {
		ASSERT_GLFW_WINDOW_NOT_NULL(window_);
		// Input processing
		processInput();

		glClearColor(color_.r, color_.g, color_.b, color_.a);

		glClear(GL_COLOR_BUFFER_BIT);

		// --- Actual Render Logic Goes Here ---

		// Swap the front and back buffers to display the rendered image
		glfwSwapBuffers(window_);

		// Check and call events (like input, window resize, etc.)
		glfwPollEvents();
	}


	void SetClearColor(const Color& color) override {
		color_ = color;
		glClearColor(color.r, color.g, color.b, color.a);
	}

	GLFWwindow* GetGLFWwindow() override {
		return window_;
	}



private:
	void processInput() {
		if (glfwGetKey(window_, GLFW_KEY_ESCAPE) == GLFW_PRESS)
			glfwSetWindowShouldClose(window_, true);
	}

	GLFWwindow* window_;
	int width_; 
	int height_;
	Color color_;
	std::string title_;
};

std::unique_ptr<Window> Window::Create(int width, int height, const std::string& title) {
	return std::make_unique<WindowImpl>(width, height, title);
}	
} // namespace wlw::core