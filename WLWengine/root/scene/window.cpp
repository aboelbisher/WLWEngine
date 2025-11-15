#ifdef WLW_USE_GLFW
#include "window.h"
#include <GLFW/glfw3.h>
#include <glad/glad.h>


#include "core/logger.h"
#include <iostream>
#include <unordered_map>

#include "scene/fps_camera.h"


namespace wlw::core {

#define ASSERT_GLFW_WINDOW_NOT_NULL(window) \
		if (window == NULL) { \
				std::cerr << "GLFW window is NULL" << std::endl; \
				return ; \
		}

class WindowImpl: public Window {
public:
	WindowImpl(const Vector2& size, const std::string& title): size_(size), title_(title), color_(1.0, 0.0, 0.0, 1.0) {}
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


	void SetClearColor(const Color& color) override {
		color_ = color;
		glClearColor(color.r, color.g, color.b, color.a);
		glClear(GL_COLOR_BUFFER_BIT);

	}

	GLFWwindow* GetGLFWwindow() override {
		return window_;
	}

	void SetIsActive(bool is_active) override {
		is_active_ = is_active;
	}

	int AddNode(const std::shared_ptr<scene::Node<Vertex2D>>& node_2d) override {
		nodes_2d_[nodes_2d_next_id_] = node_2d;
		return nodes_2d_next_id_++;
	}

	int AddNode(const std::shared_ptr<scene::Node<Vertex3D>>& node_3d) override {
		nodes_3d_[nodes_3d_next_id_] = node_3d;
		return nodes_3d_next_id_++;
	}

	const Nodes2DMap& GetNodes2D() const override {
		return nodes_2d_;
	}

	const Nodes3DMap& GetNodes3D() const override {
		return nodes_3d_;
	}

	void SetCamera(std::shared_ptr<scene::Camera3D> camera) override {
		camera_ = camera;
	}

	const std::shared_ptr<scene::Camera3D> GetUpdatedCamera() const override {
		camera_->UpdateSize(size_);
		return camera_;
	}

	Vector2 GetSize() const override {
		return size_;
	}

private:

	//TODO: Move to somehwere else (like input manager)
	void processInput() {
	
		float currentSpeedFactor = 1.0f;

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


	void ProcessMouseMovement(double xpos, double ypos) {
		std::cout << "Mouse moved to: " << xpos << ", " << ypos << std::endl;
		if (first_mouse_) {
			last_position_.x = xpos;
			last_position_.y = ypos;
			first_mouse_ = false;
		}

		// 1. Calculate the offset (how much the mouse moved)
		float xoffset = xpos - last_position_.x;
		std::cout << "X Offset: " << xoffset << std::endl;
		float yoffset = last_position_.y - ypos; // Reversed Y-offset for screen coordinates!

		// 2. Store the new position for the next frame
		last_position_.x = xpos;
		last_position_.y = ypos;

		// 3. Process with the camera object
		camera_->ProcessMouseMovement(xoffset, yoffset);


		return;
	}

	bool first_mouse_ = true;
	core::Vector2 last_position_;

	GLFWwindow* window_;
	core::Vector2 size_;
	Color color_;
	std::string title_;

	bool is_active_ = true;

	Nodes2DMap nodes_2d_;
	int nodes_2d_next_id_ = 0;

	Nodes3DMap nodes_3d_;
	int nodes_3d_next_id_ = 0;

	std::shared_ptr<scene::Camera3D> camera_ = scene::FPSCamera::Create();
};

std::unique_ptr<Window> Window::Create(const Vector2& size, const std::string& title) {
	return std::make_unique<WindowImpl>(size, title);
}	
} // namespace wlw::core

#endif // WLW_USE_GLFW