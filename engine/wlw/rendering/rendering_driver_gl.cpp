#ifdef WLW_RENDERER_GL

#include <vector>

#include "wlw/rendering/gl.h"

#include "shaders/basic_shaders.h"
#include "shaders/skybox_shaders.h"

#include "wlw/core/logger.h"
#include "wlw/platform/platform.h"
#include "rendering_driver.h"
#include "wlw/rendering/render_device.h"
#include "wlw/rendering/gl_index_buffer.h"
#include "wlw/rendering/gl_instance_buffer.h"
#include "wlw/rendering/gl_vertex_buffer.h"

namespace wlw::rendering {


class GLRenderingDriver : public RenderingDriver {
public:
    struct UniformLocations {
        GLint model;
        GLint view;
        GLint projection;
        GLint viewPos;
        GLint viewOrthographic;
        GLint viewForward;
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
        GLint instanced;
        
        GLint skyboxView;
        GLint skyboxProj;
    } m_Uniforms;

    GLRenderingDriver(RenderDevice* device) : device_(device) {}

    RenderDevice* GetDevice() override { return device_; }

	bool Initialize(std::shared_ptr<scene::Window> window) override {

    main_window_ = window;

    // The platform layer creates the OS window and its GL context (GLFW on desktop, EGL on
    // Android); the renderer only needs a current context and loaded GL functions.
    if (!platform::Init()) {
      WLW_LOG_ERROR("Platform initialization failed");
      return false;
    }
    if (!window->Initialize()) {
      WLW_LOG_ERROR("Window / GL context creation failed");
      return false;
    }
    window->MakeContextCurrent();
    if (!platform::LoadGLFunctions()) {
      WLW_LOG_ERROR("Failed to load OpenGL functions");
      return false;
    }

    WLW_LOG_INFO("--- OpenGL renderer initialized ---\n  Vendor:   %s\n  Renderer: %s\n  Version:  %s",
                 reinterpret_cast<const char*>(glGetString(GL_VENDOR)),
                 reinterpret_cast<const char*>(glGetString(GL_RENDERER)),
                 reinterpret_cast<const char*>(glGetString(GL_VERSION)));

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
    m_Uniforms.viewOrthographic = glGetUniformLocation(m_ShaderID_3D, "viewOrthographic");
    m_Uniforms.viewForward = glGetUniformLocation(m_ShaderID_3D, "viewForward");
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
    m_Uniforms.instanced = glGetUniformLocation(m_ShaderID_3D, "instanced");

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
    window->Initialize(main_window_.get());  // shares GPU resources with the main window
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
        glUniformMatrix4fv(m_Uniforms.skyboxView, 1, GL_FALSE, glm::value_ptr(staticView));
        glUniformMatrix4fv(m_Uniforms.skyboxProj, 1, GL_FALSE, glm::value_ptr(proj));
        
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
    // The projection matrix tells the two kinds apart: w is 1 for orthographic (no divide by depth),
    // -z for perspective. Row 2 of the view matrix is the camera's backward axis in world space.
    glUniform1i(m_Uniforms.viewOrthographic, proj[3][3] == 1.0f);
    glUniform3f(m_Uniforms.viewForward, -view[0][2], -view[1][2], -view[2][2]);

    scene::Frustum frustum = scene::Frustum::FromMatrix(proj * view);

    scene::Window::RenderStats stats;
    window->IterateOver3DNodes([this, &frustum, &stats](const std::shared_ptr<scene::Node3D>& node) {

      if (!node->IsVisibleInHierarchy()) {
          return;
      }
      const bool instanced = node->IsInstanced();
      if (instanced && node->GetInstances().empty()) {
          return;  // instanced with zero copies: nothing to draw
      }

      if (!frustum.TestAABB(node->GetAABB())) {
          ++stats.culled_nodes;
          return;
      }

      auto model = node->GetModel();

      if (model == nullptr || model->meshes.empty()) {
        return;
      }

      glUniformMatrix4fv(m_Uniforms.model, 1, GL_FALSE, glm::value_ptr(node->GetModelMatrix()));

      // Instanced: one draw call per mesh for all copies. The per-instance data goes to the GPU
      // only when it changed (GLInstanceBuffer::Upload compares versions).
      GLInstanceBuffer* instance_buffer = nullptr;
      if (instanced) {
        if (!node->GetInstanceBuffer()) node->SetInstanceBuffer(std::make_unique<GLInstanceBuffer>());
        instance_buffer = static_cast<GLInstanceBuffer*>(node->GetInstanceBuffer());
        instance_buffer->Upload(node->GetInstances(), node->GetInstancesVersion());
      }
      glUniform1i(m_Uniforms.instanced, instanced);

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

        const auto index_count = static_cast<GLsizei>(mesh->GetIndices().size());
        if (instanced) {
          const auto count = static_cast<GLsizei>(node->GetInstances().size());
          instance_buffer->Attach();
          glDrawElementsInstanced(GL_TRIANGLES, index_count, GL_UNSIGNED_INT, nullptr, count);
          GLInstanceBuffer::Detach();
          stats.instances += count;
        } else {
          DrawIndexed(static_cast<uint32_t>(index_count));
        }
        ++stats.draw_calls;

        vertex_buffer->Unbind();
        index_buffer->Unbind();
      }
    });
    glUniform1i(m_Uniforms.instanced, 0);
    window->SetRenderStats(stats);

    window->DrawOverlays();  // game UI, debug UI: on top of the scene

    // Present the frame we just drew (previously the swap happened before drawing,
    // so every frame was shown one frame late).
    window->SwapBuffers();
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
    if (!platform::IsAlive()) {
      return;
    }
    // Free our GL objects while the main window's context still exists...
    if (main_window_) {
      main_window_->MakeContextCurrent();
      glDeleteProgram(m_ShaderID_2D);
      glDeleteProgram(m_ShaderID_3D);
      glDeleteProgram(m_ShaderID_Skybox);
      glDeleteVertexArrays(1, &m_SkyboxVAO);
      glDeleteBuffers(1, &m_SkyboxVBO);
    }
    main_window_.reset();
    // ...then shut the platform down (destroys any windows / contexts still open).
    platform::Shutdown();
  }

private:
  GLuint CompileShader(GLenum type, const char* source) {
    GLuint id = glCreateShader(type);
    // Prepend the GLSL version for this GL flavour (see rendering/gl.h).
    const char* sources[] = {kGLSLHeader, source};
    glShaderSource(id, 2, sources, nullptr);
    glCompileShader(id);

    int result;
    glGetShaderiv(id, GL_COMPILE_STATUS, &result);
    if (result == GL_FALSE) {
      int length = 0;
      glGetShaderiv(id, GL_INFO_LOG_LENGTH, &length);
      std::vector<char> message(length > 1 ? length : 1, '\0');
      glGetShaderInfoLog(id, length, &length, message.data());
      WLW_LOG_ERROR("Failed to compile %s shader:\n%s", type == GL_VERTEX_SHADER ? "vertex" : "fragment", message.data());
      glDeleteShader(id);
      return 0;
    }
    return id;
  }

  GLuint CreateProgram(const char* vsSource, const char* fsSource) {
    GLuint vs = CompileShader(GL_VERTEX_SHADER, vsSource);
    GLuint fs = CompileShader(GL_FRAGMENT_SHADER, fsSource);
    if (vs == 0 || fs == 0) {
      if (vs) glDeleteShader(vs);
      if (fs) glDeleteShader(fs);
      return 0;
    }

    GLuint program = glCreateProgram();
    glAttachShader(program, vs);
    glAttachShader(program, fs);
    glLinkProgram(program);
    glDeleteShader(vs);  // the linked program keeps what it needs
    glDeleteShader(fs);

    GLint linked = GL_FALSE;
    glGetProgramiv(program, GL_LINK_STATUS, &linked);
    if (linked != GL_TRUE) {
      GLint length = 0;
      glGetProgramiv(program, GL_INFO_LOG_LENGTH, &length);
      std::vector<char> message(length > 1 ? length : 1, '\0');
      glGetProgramInfoLog(program, (GLsizei)message.size(), nullptr, message.data());
      WLW_LOG_ERROR("Failed to link shader program:\n%s", message.data());
      glDeleteProgram(program);
      return 0;
    }
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


#endif // WLW_RENDERER_GL