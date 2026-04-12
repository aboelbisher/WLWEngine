#ifdef WLW_USE_GLFW

#include <glad/glad.h>
#include <iostream>
#include <GLFW/glfw3.h>

#include "shaders/basic_shaders.h"
#include "shaders/skybox_shaders.h"

#include "core/logger.h"
#include "rendering_driver.h"
#include "rendering/render_device.h"
#include "rendering/gl_index_buffer.h"
#include "rendering/gl_vertex_buffer.h"

namespace wlw::rendering {

  void framebuffer_size_callback(GLFWwindow* window, int width, int height) {
    glViewport(0, 0, width, height);
  }

  void error_callback(int error, const char* description) {
		std::cerr << "GLFW Error " << error << ": " << description << std::endl;
  }


class GLRenderingDriver : public RenderingDriver {
public:
    struct UniformLocations {
        GLint model;
        GLint view;
        GLint projection;
        GLint viewPos;
        GLint lightType;
        GLint lightPos;
        GLint lightDir;
        GLint lightColor;
        GLint useLighting;
        GLint ambientStrength;
        GLint shininess;
        GLint lightConstant;
        GLint lightLinear;
        GLint lightQuadratic;
        GLint lightCutOff;
        GLint lightOuterCutOff;
        GLint use_texture;
        GLint model_texture;
        
        GLint skyboxView;
        GLint skyboxProj;
    } m_Uniforms;

    GLRenderingDriver(RenderDevice* device) : device_(device) {}

    RenderDevice* GetDevice() override { return device_; }

	bool Initialize(std::shared_ptr<scene::Window> window) override {

    main_window_ = window;
    glfwSetErrorCallback(error_callback);

    if (!glfwInit()) {
      std::cerr << "Failed to initialize GLFW" << std::endl;
      return false;
    }

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

#ifdef __APPLE__
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
#endif
    window->Initialize();
		window->MakeContextCurrent();
		window->ProcessEvents();

    if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress))
    {
      std::cerr << "Failed to initialize GLAD" << std::endl;
      glfwTerminate();
      return false;
    }

    std::cout << "--- OpenGL Renderer Initialized ---" << std::endl;
    std::cout << "Vendor: " << glGetString(GL_VENDOR) << std::endl;
    std::cout << "Renderer: " << glGetString(GL_RENDERER) << std::endl;
    std::cout << "Version: " << glGetString(GL_VERSION) << std::endl;
    std::cout << "-----------------------------------" << std::endl;

    m_ShaderID_2D = CreateProgram(vertex2DShaderSource, fragment2DShaderSource);
    if (m_ShaderID_2D == 0) return false;

    m_ShaderID_3D = CreateProgram(vertex3DShaderSource, fragment3DShaderSource);
    if (m_ShaderID_3D == 0) return false;

    m_ShaderID_Skybox = CreateProgram(skyboxVertexShaderSource, skyboxFragmentShaderSource);
    if (m_ShaderID_Skybox == 0) return false;

    glUseProgram(m_ShaderID_Skybox);
    glUniform1i(glGetUniformLocation(m_ShaderID_Skybox, "skybox"), 0);
    glUseProgram(0);

    m_Uniforms.model = glGetUniformLocation(m_ShaderID_3D, "model");
    m_Uniforms.view = glGetUniformLocation(m_ShaderID_3D, "view");
    m_Uniforms.projection = glGetUniformLocation(m_ShaderID_3D, "projection");
    m_Uniforms.viewPos = glGetUniformLocation(m_ShaderID_3D, "viewPos");
    m_Uniforms.lightType = glGetUniformLocation(m_ShaderID_3D, "lightType");
    m_Uniforms.lightPos = glGetUniformLocation(m_ShaderID_3D, "lightPos");
    m_Uniforms.lightDir = glGetUniformLocation(m_ShaderID_3D, "lightDir");
    m_Uniforms.lightColor = glGetUniformLocation(m_ShaderID_3D, "lightColor");
    m_Uniforms.useLighting = glGetUniformLocation(m_ShaderID_3D, "useLighting");
    m_Uniforms.ambientStrength = glGetUniformLocation(m_ShaderID_3D, "ambientStrength");
    m_Uniforms.shininess = glGetUniformLocation(m_ShaderID_3D, "shininess");
    m_Uniforms.lightConstant = glGetUniformLocation(m_ShaderID_3D, "lightConstant");
    m_Uniforms.lightLinear = glGetUniformLocation(m_ShaderID_3D, "lightLinear");
    m_Uniforms.lightQuadratic = glGetUniformLocation(m_ShaderID_3D, "lightQuadratic");
    m_Uniforms.lightCutOff = glGetUniformLocation(m_ShaderID_3D, "lightCutOff");
    m_Uniforms.lightOuterCutOff = glGetUniformLocation(m_ShaderID_3D, "lightOuterCutOff");
    m_Uniforms.use_texture = glGetUniformLocation(m_ShaderID_3D, "use_texture");
    m_Uniforms.model_texture = glGetUniformLocation(m_ShaderID_3D, "model_texture");

    m_Uniforms.skyboxView = glGetUniformLocation(m_ShaderID_Skybox, "view");
    m_Uniforms.skyboxProj = glGetUniformLocation(m_ShaderID_Skybox, "projection");

    float skyboxVertices[] = {
        -1.0f,  1.0f, -1.0f, -1.0f, -1.0f, -1.0f,  1.0f, -1.0f, -1.0f,
         1.0f, -1.0f, -1.0f,  1.0f,  1.0f, -1.0f, -1.0f,  1.0f, -1.0f,
        -1.0f, -1.0f,  1.0f, -1.0f, -1.0f, -1.0f, -1.0f,  1.0f, -1.0f,
        -1.0f,  1.0f, -1.0f, -1.0f,  1.0f,  1.0f, -1.0f, -1.0f,  1.0f,
         1.0f, -1.0f, -1.0f,  1.0f, -1.0f,  1.0f,  1.0f,  1.0f,  1.0f,
         1.0f,  1.0f,  1.0f,  1.0f,  1.0f, -1.0f,  1.0f, -1.0f, -1.0f,
        -1.0f, -1.0f,  1.0f, -1.0f,  1.0f,  1.0f,  1.0f,  1.0f,  1.0f,
         1.0f,  1.0f,  1.0f,  1.0f, -1.0f,  1.0f, -1.0f, -1.0f,  1.0f,
        -1.0f,  1.0f, -1.0f,  1.0f,  1.0f, -1.0f,  1.0f,  1.0f,  1.0f,
         1.0f,  1.0f,  1.0f, -1.0f,  1.0f,  1.0f, -1.0f,  1.0f, -1.0f,
        -1.0f, -1.0f, -1.0f, -1.0f, -1.0f,  1.0f,  1.0f, -1.0f, -1.0f,
         1.0f, -1.0f, -1.0f, -1.0f, -1.0f,  1.0f,  1.0f, -1.0f,  1.0f
    };
    glGenVertexArrays(1, &m_SkyboxVAO);
    glGenBuffers(1, &m_SkyboxVBO);
    glBindVertexArray(m_SkyboxVAO);
    glBindBuffer(GL_ARRAY_BUFFER, m_SkyboxVBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(skyboxVertices), &skyboxVertices, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);

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
    glClearColor(0.1f, 0.1f, 0.15f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
  }

  void DrawWindow(std::shared_ptr<scene::Window> window) override {
    window->MakeContextCurrent();
    window->ProcessEvents();

    auto size = window->GetSize();

    SetViewport(0, 0, (int)size.x, (int)size.y);
    Clear();

    auto camera = window->GetUpdatedCamera();
    glm::mat4 view = camera->GetViewMatrix();
    glm::mat4 proj = camera->GetProjectionMatrix();

    // --- RENDER SKYBOX FIRST ---
    auto skybox = window->GetSkybox();
    if (skybox) {
        glDisable(GL_DEPTH_TEST);
        glDepthMask(GL_FALSE);
        glDisable(GL_CULL_FACE);
        
        glUseProgram(m_ShaderID_Skybox);
        
        glm::mat4 staticView = glm::mat4(glm::mat3(view));
        glUniformMatrix4fv(glGetUniformLocation(m_ShaderID_Skybox, "view"), 1, GL_FALSE, glm::value_ptr(staticView));
        glUniformMatrix4fv(glGetUniformLocation(m_ShaderID_Skybox, "projection"), 1, GL_FALSE, glm::value_ptr(proj));
        
        glBindVertexArray(m_SkyboxVAO);
        skybox->Bind(0);
        glDrawArrays(GL_TRIANGLES, 0, 36);
        glBindVertexArray(0);
        
        glDepthMask(GL_TRUE);
        glEnable(GL_DEPTH_TEST);
    }

    glUseProgram(m_ShaderID_3D);
    glUniformMatrix4fv(m_Uniforms.view, 1, GL_FALSE, glm::value_ptr(view));
    glUniformMatrix4fv(m_Uniforms.projection, 1, GL_FALSE, glm::value_ptr(proj));
    core::Vector3 camPos = camera->GetPosition();
    glUniform3f(m_Uniforms.viewPos, camPos.x, camPos.y, camPos.z);

    scene::Frustum frustum = scene::Frustum::FromMatrix(proj * view);

    window->IterateOver3DNodes([this, window, &frustum](const std::shared_ptr<scene::Node3D> node) {

      if (!frustum.TestAABB(node->GetAABB())) {
          return;
      }

      auto model = node->GetModel();

      if (model == nullptr || model->meshes.empty()) {
        return;
      }

      glUniformMatrix4fv(m_Uniforms.model, 1, GL_FALSE, glm::value_ptr(node->GetModelMatrix()));

      for (const auto& mesh : model->meshes) {
        auto node_mat = node->GetMaterial();
        auto mesh_mat = mesh->GetMaterial();
        
        bool has_lighting = (node_mat && node_mat->GetLighting().has_value()) || 
                            (mesh_mat && mesh_mat->GetLighting().has_value());

        glUniform1i(m_Uniforms.use_texture, false);
        glUniform1i(m_Uniforms.useLighting, has_lighting);

        if (node_mat) {
          BindMaterial(node_mat);
        }
        if (mesh_mat) {
          BindMaterial(mesh_mat);
        }

        if (!mesh->GetVertexBuffer()) {
            mesh->SetVertexBuffer(device_->CreateVertexBuffer(mesh->GetVertices()));
        }
        if (!mesh->GetIndexBuffer()) {
            mesh->SetIndexBuffer(device_->CreateIndexBuffer(mesh->GetIndices()));
        }

        const auto* vertex_buffer = mesh->GetVertexBuffer();
        const auto* index_buffer = mesh->GetIndexBuffer();

        vertex_buffer->Bind();
        index_buffer->Bind();

        DrawIndexed(static_cast<uint32_t>(mesh->GetIndices().size()));

        vertex_buffer->Unbind();
        index_buffer->Unbind();
      }
    });
  }

  void BindMaterial(std::shared_ptr<rendering::Material> material) {
    if (!material) return;

    if (material->HasTexture()) {
      glUniform1i(m_Uniforms.use_texture, true);
      glActiveTexture(GL_TEXTURE0);
      material->BindTexture();
      glUniform1i(m_Uniforms.model_texture, 0); 
    }

    if (const auto& lighting = material->GetLighting()) {
      glUniform1i(m_Uniforms.useLighting, true);
      glUniform1i(m_Uniforms.lightType, static_cast<int>(lighting->type));
      glUniform3f(m_Uniforms.lightPos, lighting->position.x, lighting->position.y, lighting->position.z);
      glUniform3f(m_Uniforms.lightDir, lighting->direction.x, lighting->direction.y, lighting->direction.z);
      glUniform3f(m_Uniforms.lightColor, lighting->color.r, lighting->color.g, lighting->color.b);
      glUniform1f(m_Uniforms.ambientStrength, lighting->ambient_strength);
      glUniform1f(m_Uniforms.shininess, lighting->shininess);
      glUniform1f(m_Uniforms.lightConstant, lighting->constant);
      glUniform1f(m_Uniforms.lightLinear, lighting->linear);
      glUniform1f(m_Uniforms.lightQuadratic, lighting->quadratic);
      glUniform1f(m_Uniforms.lightCutOff, lighting->cutOff);
      glUniform1f(m_Uniforms.lightOuterCutOff, lighting->outerCutOff);
    }
  }

  void DrawIndexed(uint32_t indexCount) {
    glDrawElements(GL_TRIANGLES, indexCount, GL_UNSIGNED_INT, nullptr);
  }

  ~GLRenderingDriver() override {
    glDeleteProgram(m_ShaderID_2D);
    glDeleteProgram(m_ShaderID_3D);
    glDeleteProgram(m_ShaderID_Skybox);
    glDeleteVertexArrays(1, &m_SkyboxVAO);
    glDeleteBuffers(1, &m_SkyboxVBO);
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
  GLuint m_ShaderID_Skybox = 0;
  GLuint m_SkyboxVAO = 0;
  GLuint m_SkyboxVBO = 0;
  std::shared_ptr<scene::Window> main_window_;
  RenderDevice* device_;
};

std::unique_ptr<RenderingDriver> RenderingDriver::Create(RenderDevice* device) {
	return std::make_unique<GLRenderingDriver>(device);
}

} // namespace wlw::rendering


#endif // WLW_USE_GLFW