#include <iostream>
#include <GLFW/glfw3.h>

#include "core/logger.h"
#include "rendering_driver.h"
#include "core/gl_index_buffer.h"
#include "core/gl_vertex_buffer.h"

//#ifdef WLW_USE_GLFW

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
  const char* vertex2DShaderSource = R"(
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

  const char* fragment2DShaderSource = R"(
#version 330 core
out vec4 FragColor;
in vec3 vertexColor;
void main()
{
    FragColor = vec4(vertexColor, 1.0f);
}
)";

  const char* vertex3DShaderSource = R"(
#version 330 core
layout (location = 0) in vec3 aPos;
layout (location = 1) in vec3 aColor;
layout (location = 1) in vec3 aNormal;

out vec3 vertexColor;
out vec3 vertexNormal;
out vec3 vertexPos;

uniform mat4 model;
uniform mat4 view;
uniform mat4 projection;

void main()
{
    vec4 worldPos = model * vec4(aPos, 1.0);
    vertexNormal = mat3(transpose(inverse(model))) * aNormal;
    gl_Position = projection * view * model * vec4(aPos, 1.0);

    vertexPos = worldPos.xyz;
    vertexColor = aColor;
}
)";


  const char* fragment3DShaderSource = R"(
#version 330 core

in vec3 vertexColor;
in vec3 vertexNormal;
in vec3 vertexPos;

uniform vec3 viewPos;
uniform vec3 objectColor;
uniform vec3 lightColor;
uniform vec3 lightPos;



out vec4 FragColor;

void main()
{
    float ambientStrength = 0.1;
    float shininess = 32.0;
    vec3 ambient = ambientStrength * vertexColor;

    vec3 norm = normalize(vertexNormal);
    vec3 lightDir = normalize(lightPos - vertexPos); 

    // Calculate the angle (cosine) and ensure it's not negative (light hitting from behind)
    float diff = max(dot(norm, lightDir), 0.0);
    vec3 diffuse = diff * vertexColor;

    // --- 3. Specular Lighting ---
    // Bright spot reflecting directly at the viewer.
    
    // Direction from fragment to the viewer/camera
    vec3 viewDir = normalize(viewPos - vertexPos);
    
    // Reflection vector (R = 2 * (N . L) * N - L)
    vec3 reflectDir = reflect(-lightDir, norm); 

    // Calculate specular factor (cosine of angle between view and reflection vector, raised to shininess)
    float spec = pow(max(dot(viewDir, reflectDir), 0.0), shininess);
    vec3 specular = spec * lightColor;

    // --- 4. Final Color Calculation ---
    // Combine all lighting components and multiply by the object's base color.
    vec3 result = (ambient + diffuse + specular) * objectColor;

    FragColor = vec4(result, 1.0);}
)";


class GLRenderingDriver : public RenderingDriver {
public:

	std::unique_ptr<core::WVertexBuffer> CreateVertexBuffer(const std::vector<core::Vertex2D>& vertices) override { 
		return std::make_unique<core::GLVertexBuffer>(vertices);
	}

  std::unique_ptr<core::WVertexBuffer> CreateVertexBuffer(const std::vector<core::Vertex3D>& vertices) override {
    return  std::make_unique<core::GLVertexBuffer>(vertices);
	}

	// NEW: Resource Creation for Index Buffers
	std::unique_ptr<core::WIndexBuffer> CreateIndexBuffer(const std::vector<uint32_t>& indices) override { 
		return std::make_unique<core::GLIndexBuffer>(indices);
	}


	bool Initialize(std::shared_ptr<core::Window> window) override {

    main_window_ = window;
    // 1. Set the error callback FIRST
    glfwSetErrorCallback(error_callback);

    if (!glfwInit()) {
      std::cerr << "Failed to initialize GLFW" << std::endl;
      return false;
    }


    // Configure OpenGL version hints (Version 3.3 Core Profile)
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

#ifdef __APPLE__
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE); // Required for Mac OS X
#endif
    window->Initialize();
		window->MakeContextCurrent();
		window->ProcessEvents();

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
    m_ShaderID_2D = CreateProgram(vertex2DShaderSource, fragment2DShaderSource);
    if (m_ShaderID_2D == 0) return false;


    m_ShaderID_3D = CreateProgram(vertex3DShaderSource, fragment3DShaderSource);
    if (m_ShaderID_3D == 0) return false;


    glEnable(GL_DEPTH_TEST);
    return true;
	}

  void AttachWindow(std::shared_ptr<core::Window> window) override {
    if (FAIL_IF(main_window_ == nullptr, "null window, did u forgot to initialise the rendering driver ? ")) {
      return;
    }
    window->Initialize(main_window_->GetGLFWwindow());
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

    auto size = window->GetSize();

    //// RHI Calls - The engine code is API-agnostic here
    SetViewport(0, 0, size.x, size.y);
    Clear();

    glUseProgram(m_ShaderID_2D);

		for (const auto& [_, node] : window->GetNodes2D()) {
			auto vertex_buffer = CreateVertexBuffer(node->GetMesh().GetVertices());
			auto index_buffer = CreateIndexBuffer(node->GetMesh().GetIndices());

			vertex_buffer->Bind();
			index_buffer->Bind(); 

			DrawIndexed(static_cast<uint32_t>(node->GetMesh().GetIndices().size()));

			vertex_buffer->Unbind();
			index_buffer->Unbind();
    }

    glUseProgram(m_ShaderID_3D);
    
    for (const auto& [_, node] : window->GetNodes3D()) {

			auto camera = window->GetUpdatedCamera();
      glUniformMatrix4fv(glGetUniformLocation(m_ShaderID_3D, "model"), 1, GL_FALSE, glm::value_ptr(node->GetModelMatrix()));
      glUniformMatrix4fv(glGetUniformLocation(m_ShaderID_3D, "view"), 1, GL_FALSE, glm::value_ptr(camera->GetViewMatrix()));
      glUniformMatrix4fv(glGetUniformLocation(m_ShaderID_3D, "projection"), 1, GL_FALSE, glm::value_ptr(camera->GetProjectionMatrix()));
			auto& camera_position = camera->GetPosition();

      glm::vec3 lightPosition = glm::vec3(1.2f, 1.0f, 2.0f);
      glm::vec3 lightColour = glm::vec3(1.0f, 1.0f, 1.0f); // White light
      glm::vec3 objectColour = glm::vec3(1.0f, 0.5f, 0.31f); // Orange-red object
      glUniform3f(glGetUniformLocation(m_ShaderID_3D, "objectColor"), objectColour.x, objectColour.y, objectColour.z);
      glUniform3f(glGetUniformLocation(m_ShaderID_3D, "lightColor"), lightColour.x, lightColour.y, lightColour.z);
      glUniform3f(glGetUniformLocation(m_ShaderID_3D, "lightPos"), lightPosition.x, lightPosition.y, lightPosition.z);
      glUniform3f(glGetUniformLocation(m_ShaderID_3D, "viewPos"), camera_position.x, camera_position.y, camera_position.z);

      auto vertex_buffer = CreateVertexBuffer(node->GetMesh().GetVertices());
      auto index_buffer = CreateIndexBuffer(node->GetMesh().GetIndices());

      vertex_buffer->Bind();
      index_buffer->Bind();

      DrawIndexed(static_cast<uint32_t>(node->GetMesh().GetIndices().size()));

      vertex_buffer->Unbind();
      index_buffer->Unbind();
    }

  }

  void DrawIndexed(uint32_t indexCount) {
    // Use the active shader program

    // The bound IVertexBuffer (VAO) and IIndexBuffer (EBO) state is used here.
    // We use GL_UNSIGNED_INT because our index data is uint32_t.
    glDrawElements(GL_TRIANGLES, indexCount, GL_UNSIGNED_INT, nullptr);
  }

  ~GLRenderingDriver() override {
    glDeleteProgram(m_ShaderID_2D);
  }

private:
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

private:
  GLuint m_ShaderID_2D = 0; 
  GLuint m_ShaderID_3D = 0; 
  std::shared_ptr<core::Window> main_window_;
};

std::unique_ptr<RenderingDriver> RenderingDriver::Create() {
	return std::make_unique<GLRenderingDriver>();
}

} // wlw::core


//#endif // WLW_USE_GLFW