#include "window.h"
#include <GLFW/glfw3.h>
#include <glad/glad.h>

#include "core/logger.h"
#include <iostream>
#include <unordered_map>

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

 void Initialize(GLFWwindow* master_window) override {
	if (window_ != NULL) {
			std::cerr << "GLFW window is already initialized" << std::endl;
			return;
		}
		window_ = glfwCreateWindow(width_, height_, title_.c_str(), NULL, master_window);
		if (window_ == NULL) {
			std::cerr << "Failed to create GLFW window with title " << title_ << std::endl;
			glfwTerminate();
		}
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


	Vector2 GetSize() const override {
		return { static_cast<float>(width_), static_cast<float>(height_)};
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

	bool is_active_ = true;

	Nodes2DMap nodes_2d_;
	int nodes_2d_next_id_ = 0;

	Nodes3DMap nodes_3d_;
	int nodes_3d_next_id_ = 0;
};

std::unique_ptr<Window> Window::Create(int width, int height, const std::string& title) {
	return std::make_unique<WindowImpl>(width, height, title);
}	
} // namespace wlw::core