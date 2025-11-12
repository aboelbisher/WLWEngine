// GLAD  be included BEFORE GLFW, as it includes the necessary OpenGL headers
// NOTE: Thimusts include path requires the glad folder to be inside your 
// configured Additional Include Directory (e.g., External/include/glad/glad.h)
#include <glad/glad.h>
// Include GLFW for window and context management.
#include <GLFW/glfw3.h>


#include "wlw.h"
#include <vector>
#include <iostream>
#include "core/window.h"

namespace wlw {


void framebuffer_size_callback(GLFWwindow* window, int width, int height) {
  // Update the OpenGL viewport to match the new window dimensions
  glViewport(0, 0, width, height);
}

void error_callback(int error, const char* description) {
  //fprintf(stderr, "GLFW Error %d: %s\n", error, description);
}

class WLWEngineImpl : public WLWEngine {

public:

	WLWEngineImpl() {}

  void Start(std::unique_ptr<wlw::core::Window> window) override {

    // 1. Set the error callback FIRST
    glfwSetErrorCallback(error_callback);

    if (!glfwInit()) {
      std::cerr << "Failed to initialize GLFW" << std::endl;
      return;
    }


    // Configure OpenGL version hints (Version 3.3 Core Profile)
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

#ifdef __APPLE__
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE); // Required for Mac OS X
#endif

		AttachWindow(std::move(window));
    core::Window* wnd = windows_.back().get();
		wnd->MakeContextCurrent();

    // Load all OpenGL function pointers required for rendering.
    if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress))
    {
      std::cerr << "Failed to initialize GLAD" << std::endl;

      const char* description;
      int error_code = glfwGetError(&description);

      if (error_code != GLFW_NO_ERROR) {
        fprintf(stderr, "Last GLFW Error: %d - %s\n", error_code, description);
      }
      glfwTerminate();
    }
  }
   
  void AttachWindow(std::unique_ptr<core::Window > window) override {
		window->Initialize();
    windows_.push_back(std::move(window));
    core::Window* wnd = windows_.back().get();
    glfwSetFramebufferSizeCallback(wnd->GetGLFWwindow(), framebuffer_size_callback);
  }


  void Iterate() override {
    for (auto& window : windows_) {
      window->MakeContextCurrent();
      window->ProcessEvents();
		}
  }


private:
	std::vector<std::unique_ptr<core::Window>> windows_;

};

std::unique_ptr<WLWEngine> WLWEngine::Create() {
	return std::make_unique<WLWEngineImpl>();
}


} // namespace wlw