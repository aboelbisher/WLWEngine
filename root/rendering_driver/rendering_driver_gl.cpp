#include "rendering_driver.h"
#include "core/gl_index_buffer.h"
#include "core/gl_vertex_buffer.h"
#include <iostream>
#include <GLFW/glfw3.h>

#ifdef WLW_USE_GLFW

namespace wlw::rendering {

  void framebuffer_size_callback(GLFWwindow* window, int width, int height) {
    // Update the OpenGL viewport to match the new window dimensions
    glViewport(0, 0, width, height);
  }

  void error_callback(int error, const char* description) {
    //fprintf(stderr, "GLFW Error %d: %s\n", error, description);
  }


  // --- Global Setup (Minimal Shaders for Modern GL) ---
// Shaders are required for modern OpenGL (Core Profile 3.3+)
  const char* vertexShaderSource = R"(
#version 330 core
layout (location = 0) in vec2 aPos;
layout (location = 1) in vec3 aColor;
out vec3 vertexColor;
void main()
{
    gl_Position = vec4(aPos.x, aPos.y, 0.0, 1.0);
    vertexColor = aColor;
}
)";

  const char* fragmentShaderSource = R"(
#version 330 core
out vec4 FragColor;
in vec3 vertexColor;
void main()
{
    FragColor = vec4(vertexColor, 1.0f);
}
)";



class GLRenderingDriver : public RenderingDriver {
public:

	std::unique_ptr<core::WVertexBuffer> CreateVertexBuffer(const std::vector<core::Vertex2D>& vertices) override { 
		return std::make_unique<core::GLVertexBuffer>(vertices);
	}

	// NEW: Resource Creation for Index Buffers
	std::unique_ptr<core::WIndexBuffer> CreateIndexBuffer(const std::vector<uint32_t>& indices) override { 
		return std::make_unique<core::GLIndexBuffer>(indices);
	}


	bool Initialize(std::shared_ptr<core::Window> window) override {

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

		window->MakeContextCurrent();


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


    // Load OpenGL function pointers using GLAD (unchanged)
    if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)) {
      std::cerr << "Failed to initialize GLAD" << std::endl;
      return false;
    }

    std::cout << "--- OpenGL Renderer Initialized ---" << std::endl;
    std::cout << "Vendor: " << glGetString(GL_VENDOR) << std::endl;
    std::cout << "Renderer: " << glGetString(GL_RENDERER) << std::endl;
    std::cout << "Version: " << glGetString(GL_VERSION) << std::endl;
    std::cout << "-----------------------------------" << std::endl;

    // Compile and link the basic shader (unchanged)
    m_ShaderID = CreateProgram(vertexShaderSource, fragmentShaderSource);
    if (m_ShaderID == 0) return false;
    glUseProgram(m_ShaderID);

    glEnable(GL_DEPTH_TEST);
    return true;
	}

  void AttachWindow(std::shared_ptr<core::Window> window) override {
    glfwSetFramebufferSizeCallback(window->GetGLFWwindow(), framebuffer_size_callback);
  }

  void SetViewport(int x, int y, int width, int height) override {
    glViewport(x, y, width, height);
  }

  void Clear() override {
    glClearColor(0.1f, 0.1f, 0.15f, 1.0f); // Dark blue-gray background
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
  }

  void DrawWindow(std::shared_ptr<core::Window> window) override {
		window->MakeContextCurrent();
    window->ProcessEvents();

    int width, height;
    glfwGetFramebufferSize(window->GetGLFWwindow(), &width, &height);

    // RHI Calls - The engine code is API-agnostic here
    SetViewport(0, 0, width, height);
    Clear();


		for (const auto& [_, node] : window->GetNodes2D()) {
			auto vertex_buffer = CreateVertexBuffer(node->GetVertices());
			auto index_buffer = CreateIndexBuffer(node->GetIndices());

			vertex_buffer->Bind();
			index_buffer->Bind(); 

			DrawIndexed(static_cast<uint32_t>(node->GetIndices().size()));  

			vertex_buffer->Unbind();
			index_buffer->Unbind();
    }
  }

  void DrawIndexed(uint32_t indexCount) {
    // Use the active shader program
    glUseProgram(m_ShaderID);

    // The bound IVertexBuffer (VAO) and IIndexBuffer (EBO) state is used here.
    // We use GL_UNSIGNED_INT because our index data is uint32_t.
    glDrawElements(GL_TRIANGLES, indexCount, GL_UNSIGNED_INT, nullptr);
  }

  ~GLRenderingDriver() override {
    glDeleteProgram(m_ShaderID);
  }


private:
  GLuint m_ShaderID = 0; // Program ID for the minimal shader

  GLuint CompileShader(GLenum type, const char* source) {
    GLuint id = glCreateShader(type);
    glShaderSource(id, 1, &source, nullptr);
    glCompileShader(id);

    int result;
    glGetShaderiv(id, GL_COMPILE_STATUS, &result);
    if (result == GL_FALSE) {
      int length;
      glGetShaderiv(id, GL_INFO_LOG_LENGTH, &length);
      std::vector<char> message(length);
      glGetShaderInfoLog(id, length, &length, message.data());
      std::cerr << "Failed to compile shader (" << (type == GL_VERTEX_SHADER ? "Vertex" : "Fragment") << "):" << std::endl;
      std::cerr << message.data() << std::endl;
      glDeleteShader(id);
      return 0;
    }
    return id;
  }

  // Helper to link program 
  GLuint CreateProgram(const char* vsSource, const char* fsSource) {
    GLuint program = glCreateProgram();
    GLuint vs = CompileShader(GL_VERTEX_SHADER, vsSource);
    GLuint fs = CompileShader(GL_FRAGMENT_SHADER, fsSource);

    glAttachShader(program, vs);
    glAttachShader(program, fs);
    glLinkProgram(program);
    glValidateProgram(program);

    glDeleteShader(vs);
    glDeleteShader(fs);

    return program;
  }


};

std::unique_ptr<RenderingDriver> RenderingDriver::Create() {
	return std::make_unique<GLRenderingDriver>();
}

} // wlw::core


#endif // WLW_USE_GLFW