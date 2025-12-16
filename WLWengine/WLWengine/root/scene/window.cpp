#ifdef WLW_USE_GLFW
#include "window.h"

#include <GLFW/glfw3.h>


#include "core/logger.h"
#include <iostream>
#include <unordered_map>

#include "scene/fps_camera.h"


namespace wlw::scene {

#define ASSERT_GLFW_WINDOW_NOT_NULL(window) \
		if (window == NULL) { \
				std::cerr << "GLFW window is NULL" << std::endl; \
				return ; \
		}

class WindowImpl: public Window {
public:
	WindowImpl(const core::Vector2& size, const std::string& title) : title_(title), color_(1.0, 0.0, 0.0, 1.0) {
		size_ = size;
	}
	~WindowImpl() override {
		// Destructor implementation (e.g., destroy the window)
	}

 void Initialize(GLFWwindow* master_window) override {
	if (window_ != NULL) {
			std::cerr << "GLFW window is already initialized" << std::endl;
			return;
		}
		window_ = glfwCreateWindow(size_.x, size_.y, title_.c_str(), NULL, master_window);
		if (window_ == NULL) {
			std::cerr << "Failed to create GLFW window with title " << title_ << std::endl;
			glfwTerminate();
		}

		glfwSetInputMode(window_, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
	}


	void MakeContextCurrent() override {
		ASSERT_GLFW_WINDOW_NOT_NULL(window_);
		glfwMakeContextCurrent(window_);
		glfwSwapInterval(1);
	}

	void ProcessEvents() override {
		ASSERT_GLFW_WINDOW_NOT_NULL(window_);
		// Input processing
		processInput();

		// --- Actual Render Logic Goes Here ---

		// Swap the front and back buffers to display the rendered image
		glfwSwapBuffers(window_);

		// Check and call events (like input, window resize, etc.)
		glfwPollEvents();
	}


	void SetClearColor(const core::Color& color) override {
		color_ = color;
		glClearColor(color.r, color.g, color.b, color.a);
		glClear(GL_COLOR_BUFFER_BIT);

	}

	GLFWwindow* GetGLFWwindow() override {
		return window_;
	}


private:

	//TODO: Move to somehwere else (like input manager)
	void processInput() {
	
		float currentSpeedFactor = 0.5f;

		float deltaTime = 0.016f; // Assuming a fixed time step for simplicity (60 FPS)
		// Forward
		if (glfwGetKey(window_, GLFW_KEY_W) == GLFW_PRESS) {
			if (glfwGetKey(window_, GLFW_KEY_LEFT_CONTROL)) {
				camera_->ProcessKeyboard(scene::CameraMovement::UP_ROTATE, deltaTime * currentSpeedFactor);
			}
			else {
				camera_->ProcessKeyboard(scene::CameraMovement::FORWARD, deltaTime * currentSpeedFactor);
			}
		}
		// Backward
		if ( glfwGetKey(window_, GLFW_KEY_S) == GLFW_PRESS ) {
			if (glfwGetKey(window_, GLFW_KEY_LEFT_CONTROL)) {
				camera_->ProcessKeyboard(scene::CameraMovement::DOWN_ROTATE, deltaTime * currentSpeedFactor);
			}
			else {
				camera_->ProcessKeyboard(scene::CameraMovement::BACKWARD, deltaTime * currentSpeedFactor);
			}

		}
		// Left (Strafe)
		if (glfwGetKey(window_, GLFW_KEY_A) == GLFW_PRESS ) {
			if (glfwGetKey(window_, GLFW_KEY_LEFT_CONTROL)) {
				camera_->ProcessKeyboard(scene::CameraMovement::LEFT_ROTATE, deltaTime * currentSpeedFactor);
			}
			else {
				camera_->ProcessKeyboard(scene::CameraMovement::LEFT, deltaTime * currentSpeedFactor);
			}
		}
		// Right (Strafe)
		if (glfwGetKey(window_, GLFW_KEY_D) == GLFW_PRESS ) {
			if (glfwGetKey(window_, GLFW_KEY_LEFT_CONTROL)) {
				camera_->ProcessKeyboard(scene::CameraMovement::RIGHT_ROTATE, deltaTime * currentSpeedFactor);
			}
			else {
				camera_->ProcessKeyboard(scene::CameraMovement::RIGHT, deltaTime * currentSpeedFactor);
			}

		}
		// Up (Elevation)
		if ( glfwGetKey(window_, GLFW_KEY_E) == GLFW_PRESS ) {
			camera_->ProcessKeyboard(scene::CameraMovement::UP, deltaTime * currentSpeedFactor);
		}
		// Down (Elevation)
		if (glfwGetKey(window_, GLFW_KEY_Q) == GLFW_PRESS) {
			camera_->ProcessKeyboard(scene::CameraMovement::DOWN, deltaTime * currentSpeedFactor);
		}
	}

	void ProcessMouseMovement(double xpos, double ypos) override {
		if (first_mouse_) {
			last_position_.x = xpos;
			last_position_.y = ypos;
			first_mouse_ = false;
		}

		float xoffset = xpos - last_position_.x;
		float yoffset = last_position_.y - ypos;

		last_position_.x = xpos;
		last_position_.y = ypos;

		camera_->ProcessMouseMovement(xoffset, yoffset);
	}

	bool first_mouse_ = true;
	core::Vector2 last_position_;

	GLFWwindow* window_;
	core::Color color_;
	std::string title_;

};

std::unique_ptr<Window> Window::Create(const core::Vector2& size, const std::string& title) {
	return std::make_unique<WindowImpl>(size, title);
}	
} // namespace wlw::core

#endif // WLW_USE_GLFW