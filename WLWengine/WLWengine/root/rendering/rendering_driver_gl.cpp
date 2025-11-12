#ifdef WLW_USE_GLFW

#include <glad/glad.h>
#include <iostream>
#include <GLFW/glfw3.h>

#include "shaders/basic_shaders.h"

#include "core/logger.h"
#include "rendering_driver.h"
#include "rendering/gl_index_buffer.h"
#include "rendering/gl_vertex_buffer.h"

//#ifdef WLW_USE_GLFW

namespace wlw::rendering {

  void framebuffer_size_callback(GLFWwindow* window, int width, int height) {
    // Update the OpenGL viewport to match the new window dimensions
    glViewport(0, 0, width, height);
  }

  void error_callback(int error, const char* description) {
		std::cerr << "GLFW Error " << error << ": " << description << std::endl;
  }


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


	bool Initialize(std::shared_ptr<scene::Window> window) override {

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

  void AttachWindow(std::shared_ptr<scene::Window> window) override {
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

  void DrawWindow(std::shared_ptr<scene::Window> window) override {
		window->MakeContextCurrent();
    window->ProcessEvents();

    auto size = window->GetSize();

    //// RHI Calls - The engine code is API-agnostic here
    SetViewport(0, 0, size.x, size.y);
    Clear();


    //2D rendering
    /*

    glUseProgram(m_ShaderID_2D);

  	for (const auto& [_, node] : window->GetNodes2D()) {
			auto vertex_buffer = CreateVertexBuffer(node->GetMesh().GetVertices());
			auto index_buffer = CreateIndexBuffer(node->GetMesh().GetIndices());

			vertex_buffer->Bind();
			index_buffer->Bind(); 

			DrawIndexed(static_cast<uint32_t>(node->GetMesh().GetIndices().size()));

			vertex_buffer->Unbind();
			index_buffer->Unbind();
    }*/

    glUseProgram(m_ShaderID_3D);
    
    window->IterateOver3DNodes([this, window](const std::shared_ptr<scene::Node3D> node) {

      auto model = node->GetModel();
      if (model == nullptr || model->meshes.empty()) {
        return;
      }

      auto camera = window->GetUpdatedCamera();
      glUniformMatrix4fv(glGetUniformLocation(m_ShaderID_3D, "model"), 1, GL_FALSE, glm::value_ptr(node->GetModelMatrix()));
      glUniformMatrix4fv(glGetUniformLocation(m_ShaderID_3D, "view"), 1, GL_FALSE, glm::value_ptr(camera->GetViewMatrix()));
      glUniformMatrix4fv(glGetUniformLocation(m_ShaderID_3D, "projection"), 1, GL_FALSE, glm::value_ptr(camera->GetProjectionMatrix()));

      for (const auto& mesh : model->meshes) {
        //auto material = mesh->GetMaterial();
        //if (material = nullptr || !material->HasTexture()) {
        //  continue;
        //}

       BindMaterial(mesh->GetMaterial(), camera->GetPosition());
       BindMaterial(node->GetMaterial(), camera->GetPosition());

        auto vertex_buffer = CreateVertexBuffer(mesh->GetVertices());
        auto index_buffer = CreateIndexBuffer(mesh->GetIndices());

        vertex_buffer->Bind();
        index_buffer->Bind();

        DrawIndexed(static_cast<uint32_t>(mesh->GetIndices().size()));

        vertex_buffer->Unbind();
        index_buffer->Unbind();
      }


      });

  }

  void BindMaterial(std::shared_ptr<rendering::Material> material, const core::Vector3& camera_position) {
    if (material) {
      if (material->HasTexture()) {
        glUniform1i(glGetUniformLocation(m_ShaderID_3D, "use_texture"), true);
        glActiveTexture(GL_TEXTURE0);
        material->BindTexture();
        GLint textureLocation = glGetUniformLocation(m_ShaderID_3D, "model_texture");
        glUniform1i(textureLocation, 0); // Tell the shader to use GL_TEXTURE0
      } else {
        glUniform1i(glGetUniformLocation(m_ShaderID_3D, "use_texture"), false);
      }

      if (const auto& lighting = material->GetLighting()) {
        auto material_light_color = material->color;
        glUniform3f(glGetUniformLocation(m_ShaderID_3D, "viewPos"), camera_position.x, camera_position.y, camera_position.z);
        glUniform1i(glGetUniformLocation(m_ShaderID_3D, "useLighting"), true);
        glUniform3f(glGetUniformLocation(m_ShaderID_3D, "objectColor"), material_light_color.r, material_light_color.g, material_light_color.b);
        glUniform3f(glGetUniformLocation(m_ShaderID_3D, "lightPos"), lighting->position.x, lighting->position.y, lighting->position.z);
        glUniform1f(glGetUniformLocation(m_ShaderID_3D, "ambientStrength"), lighting->ambient_strength);
        glUniform1f(glGetUniformLocation(m_ShaderID_3D, "shininess"), lighting->shininess);
      } else {
        glUniform1i(glGetUniformLocation(m_ShaderID_3D, "useLighting"), false);
      }
    } else {
      glUniform1i(glGetUniformLocation(m_ShaderID_3D, "use_texture"), false);
      glUniform1i(glGetUniformLocation(m_ShaderID_3D, "useLighting"), false);

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
  std::shared_ptr<scene::Window> main_window_;
};

std::unique_ptr<RenderingDriver> RenderingDriver::Create() {
	return std::make_unique<GLRenderingDriver>();
}

} // wlw::core


#endif // WLW_USE_GLFW