#ifdef WLW_RENDERER_GL

#include <algorithm>
#include <array>
#include <cmath>
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
#include "wlw/rendering/gl_shader.h"
#include "wlw/rendering/gl_vertex_buffer.h"
#include "wlw/scene/light.h"
#include "wlw/scene/shadow_map.h"
#include "wlw/ui/ui_renderer.h"

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
        GLint useLighting;
        GLint lightCount;
        GLint lightTypes;
        GLint lightPositions;
        GLint lightDirections;
        GLint lightColors;
        GLint lightAmbients;
        GLint lightAttenuations;
        GLint lightCones;
        GLint shininess;
        GLint shadowLight;
        GLint receiveShadow;
        GLint shadowMap;
        GLint shadowStrength;
        GLint shadowSoftness;
        GLint shadowTexel;
        GLint shadowNormalOffset;
        GLint lightSpace;
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
    m_Uniforms.useLighting = glGetUniformLocation(m_ShaderID_3D, "useLighting");
    m_Uniforms.lightCount = glGetUniformLocation(m_ShaderID_3D, "lightCount");
    // Arrays: the location of element 0; glUniform*v fills the following elements from there.
    m_Uniforms.lightTypes = glGetUniformLocation(m_ShaderID_3D, "lightTypes[0]");
    m_Uniforms.lightPositions = glGetUniformLocation(m_ShaderID_3D, "lightPositions[0]");
    m_Uniforms.lightDirections = glGetUniformLocation(m_ShaderID_3D, "lightDirections[0]");
    m_Uniforms.lightColors = glGetUniformLocation(m_ShaderID_3D, "lightColors[0]");
    m_Uniforms.lightAmbients = glGetUniformLocation(m_ShaderID_3D, "lightAmbients[0]");
    m_Uniforms.lightAttenuations = glGetUniformLocation(m_ShaderID_3D, "lightAttenuations[0]");
    m_Uniforms.lightCones = glGetUniformLocation(m_ShaderID_3D, "lightCones[0]");
    m_Uniforms.shininess = glGetUniformLocation(m_ShaderID_3D, "shininess");
    m_Uniforms.shadowLight = glGetUniformLocation(m_ShaderID_3D, "shadowLight");
    m_Uniforms.receiveShadow = glGetUniformLocation(m_ShaderID_3D, "receiveShadow");
    m_Uniforms.shadowMap = glGetUniformLocation(m_ShaderID_3D, "shadowMap");
    m_Uniforms.shadowStrength = glGetUniformLocation(m_ShaderID_3D, "shadowStrength");
    m_Uniforms.shadowSoftness = glGetUniformLocation(m_ShaderID_3D, "shadowSoftness");
    m_Uniforms.shadowTexel = glGetUniformLocation(m_ShaderID_3D, "shadowTexel");
    m_Uniforms.shadowNormalOffset = glGetUniformLocation(m_ShaderID_3D, "shadowNormalOffset");
    m_Uniforms.lightSpace = glGetUniformLocation(m_ShaderID_3D, "lightSpace");
    m_Uniforms.use_texture = glGetUniformLocation(m_ShaderID_3D, "use_texture");
    m_Uniforms.model_texture = glGetUniformLocation(m_ShaderID_3D, "model_texture");
    m_Uniforms.instanced = glGetUniformLocation(m_ShaderID_3D, "instanced");
    // Two sampler types may never share a texture unit: the model texture uses 0, shadows 1.
    glUseProgram(m_ShaderID_3D);
    glUniform1i(m_Uniforms.model_texture, 0);
    glUniform1i(m_Uniforms.shadowMap, 1);
    glUniform1i(m_Uniforms.shadowLight, -1);
    glUniform1i(m_Uniforms.lightCount, 0);
    glUseProgram(0);

    m_ShaderID_Shadow = CreateProgram(vertexShadowShaderSource, fragmentShadowShaderSource);
    if (m_ShaderID_Shadow == 0) return false;
    shadow_uniforms_.model = glGetUniformLocation(m_ShaderID_Shadow, "model");
    shadow_uniforms_.lightSpace = glGetUniformLocation(m_ShaderID_Shadow, "lightSpace");
    shadow_uniforms_.instanced = glGetUniformLocation(m_ShaderID_Shadow, "instanced");

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

    // Draws each viewport's 2D canvas (the screen's is the game UI).
    ui_renderer_ = ui::UIRenderer::Create();

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

  // What one viewport draws, gathered from its scene before any drawing starts.
  struct Frame {
    std::vector<const scene::LightNode*> lights;  // visible light nodes, at most kMaxLights
    std::vector<scene::Node3D*> drawables;         // visible nodes with something to draw
    scene::AABB casters{}, receivers{};            // around every shadow caster / receiver
    bool has_casters = false, has_receivers = false;
    int shadow_light = -1;                         // index into lights
  };

  // One frame of a window: its offscreen viewports into their textures (in the order added, so
  // one may use the picture of an earlier one), then the screen, the overlays, and present.
  void DrawWindow(std::shared_ptr<scene::Window> window) override {
    window->MakeContextCurrent();

    GLint screen_framebuffer = 0;  // not always 0: the platform may draw into its own
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &screen_framebuffer);
    const glm::vec2 screen_pixels{window->GetSize().x, window->GetSize().y};

    for (const auto& [_, viewport] : window->GetViewports()) {
      RenderTarget* target = viewport->GetTarget();
      if (target == nullptr) continue;  // only the window's own GetScreen() draws to the screen
      const glm::ivec2 pixels = viewport->PixelSize(screen_pixels);
      if (!viewport->BeginDraw(pixels)) continue;
      target->EnsureSize(pixels.x, pixels.y);
      glBindFramebuffer(GL_FRAMEBUFFER, target->FramebufferId());
      DrawViewport(*viewport, pixels, target->Settings().depth);
    }

    glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(screen_framebuffer));
    DrawViewport(window->GetScreen(), {static_cast<int>(screen_pixels.x), static_cast<int>(screen_pixels.y)},
                 /*has_depth=*/true);
    window->DrawOverlays();  // e.g. the debug UI: on top of everything

    // Present the frame we just drew (previously the swap happened before drawing,
    // so every frame was shown one frame late).
    window->SwapBuffers();
  }

  // One viewport into the framebuffer that is bound: its scene through its camera (shadow pass,
  // then the main pass), then its canvas over it.
  void DrawViewport(scene::Viewport& viewport, glm::ivec2 pixels, bool has_depth) {
    const std::shared_ptr<scene::Scene>& scene = viewport.GetScene();
    const std::shared_ptr<scene::Camera3D>& camera = viewport.GetCamera();
    const bool draw_scene = scene != nullptr && camera != nullptr;

    // Gather + shadow pass first: the shadow pass draws into its own framebuffer.
    Frame frame;
    scene::ShadowFrustum shadow_frustum;
    if (draw_scene) {
      if (pixels.x > 0 && pixels.y > 0) {  // a minimized window has no size
        camera->UpdateSize({static_cast<float>(pixels.x), static_cast<float>(pixels.y)});
      }
      GatherScene(*scene, frame);
      DrawShadows(frame, shadow_frustum);
    }

    SetViewport(0, 0, pixels.x, pixels.y);
    glDepthMask(GL_TRUE);
    GLbitfield clear_bits = has_depth ? GL_DEPTH_BUFFER_BIT : 0;
    if (const auto& color = viewport.GetClearColor()) {
      glClearColor(color->r, color->g, color->b, color->a);
      clear_bits |= GL_COLOR_BUFFER_BIT;
    }
    if (clear_bits) glClear(clear_bits);

    scene::RenderStats stats;
    if (draw_scene) DrawMainPass(*scene, *camera, frame, shadow_frustum, stats);
    viewport.SetRenderStats(stats);

    ui_renderer_->Draw(viewport.GetCanvas(), {static_cast<float>(pixels.x), static_cast<float>(pixels.y)});
  }

  // --- 1. GATHER: lights, things to draw, and the bounds of everything shadows involve ---
  void GatherScene(const scene::Scene& scene, Frame& frame) {
    scene.IterateOverNodes([&frame](const std::shared_ptr<scene::Node3D>& node) {
      if (!node->IsVisibleInHierarchy()) return;
      if (const scene::LightNode* light = node->AsLight()) {
        if (static_cast<int>(frame.lights.size()) < scene::LightNode::kMaxLights) frame.lights.push_back(light);
        return;
      }
      auto model = node->GetModel();
      if (model == nullptr || model->meshes.empty()) return;
      if (node->IsInstanced() && node->GetInstances().empty()) return;  // instanced with zero copies
      frame.drawables.push_back(node.get());
      if (node->CastsShadow()) Grow(frame.casters, frame.has_casters, node->GetAABB());
      if (node->ReceivesShadow()) Grow(frame.receivers, frame.has_receivers, node->GetAABB());
    });

    // The shadow light: the first directional light that casts shadows.
    for (int i = 0; i < static_cast<int>(frame.lights.size()); ++i) {
      const scene::LightNode* light = frame.lights[i];
      if (!light->CastsShadows()) continue;
      if (light->GetType() != scene::LightNode::Type::Directional) {
        if (!warned_shadow_type_) WLW_LOG_WARN("Only directional lights cast shadows (for now): ignoring the others");
        warned_shadow_type_ = true;
        continue;
      }
      if (frame.shadow_light < 0) frame.shadow_light = i;
    }
  }

  // --- 2. SHADOW PASS: the depth of every caster, seen from the shadow light ---
  void DrawShadows(Frame& frame, scene::ShadowFrustum& shadow_frustum) {
    if (frame.shadow_light >= 0 && frame.has_casters) {
      const scene::LightNode* light = frame.lights[frame.shadow_light];
      shadow_frustum = scene::FitDirectionalShadow(light->GetDirection(), frame.casters,
                                                   frame.has_receivers ? frame.receivers : frame.casters, light->shadow.map_size);
      if (!DrawShadowMap(frame, light->shadow.map_size, shadow_frustum.ViewProjection())) frame.shadow_light = -1;
    } else {
      frame.shadow_light = -1;
    }
  }

  // --- 3. MAIN PASS: skybox, then every visible drawable, lit (and shadowed) ---
  void DrawMainPass(const scene::Scene& scene, scene::Camera3D& camera, const Frame& frame,
                    const scene::ShadowFrustum& shadow_frustum, scene::RenderStats& stats) {
    glm::mat4 view = camera.GetViewMatrix();
    glm::mat4 proj = camera.GetProjectionMatrix();

    // Skybox first.
    if (const auto& skybox = scene.GetSkybox()) {
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
    core::Vector3 camPos = camera.GetPosition();
    frame_view_ = view;  // for custom shaders (UseCustomShader)
    frame_projection_ = proj;
    frame_camera_ = {camPos.x, camPos.y, camPos.z};
    glUniform3f(m_Uniforms.viewPos, camPos.x, camPos.y, camPos.z);
    // The projection matrix tells the two kinds apart: w is 1 for orthographic (no divide by depth),
    // -z for perspective. Row 2 of the view matrix is the camera's backward axis in world space.
    glUniform1i(m_Uniforms.viewOrthographic, proj[3][3] == 1.0f);
    glUniform3f(m_Uniforms.viewForward, -view[0][2], -view[1][2], -view[2][2]);

    // Shadows: the map on texture unit 1 (unit 0 is the model texture).
    glUniform1i(m_Uniforms.shadowLight, frame.shadow_light);
    if (frame.shadow_light >= 0) {
      const scene::ShadowSettings& settings = frame.lights[frame.shadow_light]->shadow;
      glActiveTexture(GL_TEXTURE1);
      glBindTexture(GL_TEXTURE_2D, shadow_texture_);
      glActiveTexture(GL_TEXTURE0);
      glUniformMatrix4fv(m_Uniforms.lightSpace, 1, GL_FALSE, glm::value_ptr(shadow_frustum.ViewProjection()));
      glUniform1f(m_Uniforms.shadowStrength, settings.strength);
      glUniform1f(m_Uniforms.shadowSoftness, settings.softness);
      glUniform1f(m_Uniforms.shadowTexel, 1.0f / static_cast<float>(shadow_map_size_));
      glUniform1f(m_Uniforms.shadowNormalOffset, shadow_frustum.texel_world * 1.5f);
    } else {
      glUniformMatrix4fv(m_Uniforms.lightSpace, 1, GL_FALSE, glm::value_ptr(glm::mat4(1.0f)));
      glUniform1f(m_Uniforms.shadowNormalOffset, 0.0f);
    }
    // Scene lights, if there are any, light everything; otherwise each material's own light does.
    const bool material_lights = frame.lights.empty();
    if (!material_lights) UploadSceneLights(frame.lights);

    scene::Frustum frustum = scene::Frustum::FromMatrix(proj * view);

    for (scene::Node3D* node : frame.drawables) {
      if (!frustum.TestAABB(node->GetAABB())) {
          ++stats.culled_nodes;
          continue;
      }
      glUniform1i(m_Uniforms.receiveShadow, node->ReceivesShadow());
      DrawNode(*node, /*shadow_pass=*/false, material_lights, stats);
    }
    glUniform1i(m_Uniforms.instanced, 0);
  }

  static void Grow(scene::AABB& box, bool& has_box, const scene::AABB& add) {
    if (!has_box) {
      box = add;
      has_box = true;
      return;
    }
    box.min = {std::min(box.min.x, add.min.x), std::min(box.min.y, add.min.y), std::min(box.min.z, add.min.z)};
    box.max = {std::max(box.max.x, add.max.x), std::max(box.max.y, add.max.y), std::max(box.max.z, add.max.z)};
  }

  // Draws one node's meshes with the program in use. In the shadow pass only geometry matters;
  // in the main pass each mesh's material sets its texture (and its light, when the scene has none).
  void DrawNode(scene::Node3D& node, bool shadow_pass, bool material_lights, scene::RenderStats& stats) {
    const GLint model_location = shadow_pass ? shadow_uniforms_.model : m_Uniforms.model;
    const GLint instanced_location = shadow_pass ? shadow_uniforms_.instanced : m_Uniforms.instanced;
    glUniformMatrix4fv(model_location, 1, GL_FALSE, glm::value_ptr(node.GetModelMatrix()));

    // Instanced: one draw call per mesh for all copies. The per-instance data goes to the GPU
    // only when it changed (GLInstanceBuffer::Upload compares versions).
    const bool instanced = node.IsInstanced();
    GLInstanceBuffer* instance_buffer = nullptr;
    if (instanced) {
      if (!node.GetInstanceBuffer()) node.SetInstanceBuffer(std::make_unique<GLInstanceBuffer>());
      instance_buffer = static_cast<GLInstanceBuffer*>(node.GetInstanceBuffer());
      instance_buffer->Upload(node.GetInstances(), node.GetInstancesVersion());
    }
    glUniform1i(instanced_location, instanced);

    for (const auto& mesh : node.GetModel()->meshes) {
      // A ShaderMaterial draws the mesh with its own shader instead (the mesh's material first).
      const ShaderMaterial* custom = nullptr;
      if (!shadow_pass) {
        custom = DrawableShaderMaterial(mesh->GetMaterial());
        if (!custom) custom = DrawableShaderMaterial(node.GetMaterial());
      }
      if (custom) {
        UseShaderMaterial(*custom, node.GetModelMatrix(), instanced);
      } else if (!shadow_pass) {
        glUniform1i(m_Uniforms.use_texture, false);
        if (material_lights) glUniform1i(m_Uniforms.useLighting, false);
        BindMaterial(node.GetMaterial(), material_lights);
        BindMaterial(mesh->GetMaterial(), material_lights);  // the mesh's own material wins
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
        const auto count = static_cast<GLsizei>(node.GetInstances().size());
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
      if (custom) glUseProgram(m_ShaderID_3D);  // back to the engine's for the next mesh
    }
  }

  // The material as a ShaderMaterial with a working 3D shader, else null (an invalid one falls
  // back to the engine's shader).
  static const ShaderMaterial* DrawableShaderMaterial(const std::shared_ptr<rendering::Material>& material) {
    const ShaderMaterial* shader_material = material ? material->AsShaderMaterial() : nullptr;
    if (!shader_material || !shader_material->IsValid()) return nullptr;
    if (shader_material->GetShader()->GetTarget() != Shader::Target::Mesh) return nullptr;
    return shader_material;
  }

  // Switches to a ShaderMaterial's shader and gives it the engine's inputs (see shader.h) and the
  // material's params. Its textures start at unit 2: units 0-1 belong to the engine's shader
  // (model texture, shadow map), which stay bound for the nodes drawn after.
  void UseShaderMaterial(const ShaderMaterial& material, const glm::mat4& model, bool instanced) {
    const auto& shader = static_cast<const GLShader&>(*material.GetShader());
    glUseProgram(shader.Program());
    glUniformMatrix4fv(shader.Location("uModel"), 1, GL_FALSE, glm::value_ptr(model));
    glUniformMatrix4fv(shader.Location("uView"), 1, GL_FALSE, glm::value_ptr(frame_view_));
    glUniformMatrix4fv(shader.Location("uProjection"), 1, GL_FALSE, glm::value_ptr(frame_projection_));
    glUniform3fv(shader.Location("uCameraPos"), 1, glm::value_ptr(frame_camera_));
    glUniform1i(shader.Location("uInstanced"), instanced);
    shader.ApplyParams(material, 2);
  }

  // Renders every shadow caster's depth into the shadow map (made or resized on demand). Returns
  // false if the map can't be made; then there are simply no shadows.
  bool DrawShadowMap(const Frame& frame, int map_size, const glm::mat4& light_space) {
    if (!EnsureShadowMap(map_size)) return false;

    GLint previous_framebuffer = 0;  // not always 0: the platform may draw into its own
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &previous_framebuffer);
    glBindFramebuffer(GL_FRAMEBUFFER, shadow_framebuffer_);
    glViewport(0, 0, shadow_map_size_, shadow_map_size_);
    glClear(GL_DEPTH_BUFFER_BIT);

    glUseProgram(m_ShaderID_Shadow);
    glUniformMatrix4fv(shadow_uniforms_.lightSpace, 1, GL_FALSE, glm::value_ptr(light_space));
    // A depth bias that grows with the surface's slope to the light: against "shadow acne".
    glEnable(GL_POLYGON_OFFSET_FILL);
    glPolygonOffset(1.5f, 3.0f);
    scene::RenderStats unused;
    for (scene::Node3D* node : frame.drawables) {
      if (node->CastsShadow()) DrawNode(*node, /*shadow_pass=*/true, false, unused);
    }
    glDisable(GL_POLYGON_OFFSET_FILL);
    glUniform1i(shadow_uniforms_.instanced, 0);

    glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(previous_framebuffer));
    return true;
  }

  bool EnsureShadowMap(int map_size) {
    map_size = std::clamp(map_size, 64, 4096);
    if (shadow_framebuffer_ != 0 && shadow_map_size_ == map_size) return true;
    if (shadow_failed_) return false;
    DeleteShadowMap();

    // A depth texture that compares when sampled (sampler2DShadow), with linear filtering so the
    // GPU blends the 4 nearest comparisons. Outside the map counts as lit (the shader checks).
    glGenTextures(1, &shadow_texture_);
    glBindTexture(GL_TEXTURE_2D, shadow_texture_);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24, map_size, map_size, 0, GL_DEPTH_COMPONENT, GL_UNSIGNED_INT,
                 nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_MODE, GL_COMPARE_REF_TO_TEXTURE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_FUNC, GL_LEQUAL);
    glBindTexture(GL_TEXTURE_2D, 0);

    GLint previous_framebuffer = 0;
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &previous_framebuffer);
    glGenFramebuffers(1, &shadow_framebuffer_);
    glBindFramebuffer(GL_FRAMEBUFFER, shadow_framebuffer_);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, shadow_texture_, 0);
    const GLenum none = GL_NONE;
    glDrawBuffers(1, &none);  // depth only: no colour is written...
    glReadBuffer(GL_NONE);    // ...or read
    const bool complete = glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
    glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(previous_framebuffer));
    if (!complete) {
      WLW_LOG_ERROR("Shadow map framebuffer incomplete: shadows are off");
      DeleteShadowMap();
      ui_renderer_.reset();
      shadow_failed_ = true;
      return false;
    }
    shadow_map_size_ = map_size;
    WLW_LOG_INFO("Shadow map: %d x %d", map_size, map_size);
    return true;
  }

  void DeleteShadowMap() {
    if (shadow_framebuffer_) glDeleteFramebuffers(1, &shadow_framebuffer_);
    if (shadow_texture_) glDeleteTextures(1, &shadow_texture_);
    shadow_framebuffer_ = 0;
    shadow_texture_ = 0;
    shadow_map_size_ = 0;
  }

  // The shader's light arrays, filled one light at a time.
  struct LightArrays {
    int count = 0;
    std::array<GLint, scene::LightNode::kMaxLights> types{};
    std::array<glm::vec3, scene::LightNode::kMaxLights> positions{}, directions{}, colors{}, attenuations{};
    std::array<GLfloat, scene::LightNode::kMaxLights> ambients{};
    std::array<glm::vec2, scene::LightNode::kMaxLights> cones{};

    void Add(int type, const glm::vec3& position, const glm::vec3& direction, const glm::vec3& color, float ambient,
             const glm::vec3& attenuation, const glm::vec2& cone) {
      if (count >= scene::LightNode::kMaxLights) return;
      types[count] = type;
      positions[count] = position;
      directions[count] = direction;
      colors[count] = color;
      ambients[count] = ambient;
      attenuations[count] = attenuation;
      cones[count] = cone;
      ++count;
    }
    void Upload(const UniformLocations& u) const {
      glUniform1i(u.lightCount, count);
      if (count == 0) return;
      glUniform1iv(u.lightTypes, count, types.data());
      glUniform3fv(u.lightPositions, count, glm::value_ptr(positions[0]));
      glUniform3fv(u.lightDirections, count, glm::value_ptr(directions[0]));
      glUniform3fv(u.lightColors, count, glm::value_ptr(colors[0]));
      glUniform1fv(u.lightAmbients, count, ambients.data());
      glUniform3fv(u.lightAttenuations, count, glm::value_ptr(attenuations[0]));
      glUniform2fv(u.lightCones, count, glm::value_ptr(cones[0]));
    }
  };

  // The scene's light nodes -> the shader's light arrays.
  void UploadSceneLights(const std::vector<const scene::LightNode*>& lights) {
    LightArrays arrays;
    for (const scene::LightNode* light : lights) {
      glm::vec3 attenuation(1.0f, 0.0f, 0.0f);
      glm::vec2 cone(-1.0f, -1.0f);
      float range = 0.0f;
      if (const auto* point = dynamic_cast<const scene::PointLight*>(light)) range = point->GetRange();
      if (const auto* spot = dynamic_cast<const scene::SpotLight*>(light)) {
        range = spot->GetRange();
        cone = {std::cos(glm::radians(spot->GetInnerDegrees())), std::cos(glm::radians(spot->GetOuterDegrees()))};
      }
      // A falloff that is down to about 1% at `range` (the classic constant/linear/quadratic form).
      if (range > 0.0f) attenuation = {1.0f, 4.5f / range, 75.0f / (range * range)};
      const core::Vector3 position = light->GetWorldPosition();
      arrays.Add(static_cast<int>(light->GetType()), {position.x, position.y, position.z}, light->GetDirection(),
                 light->GetColor() * light->GetIntensity(), light->GetAmbient(), attenuation, cone);
    }
    arrays.Upload(m_Uniforms);
    glUniform1i(m_Uniforms.useLighting, true);
  }

  // A material's texture, and (the older way, used when the scene has no light nodes) its light.
  void BindMaterial(const std::shared_ptr<rendering::Material>& material, bool material_lights) {
    if (!material) return;

    if (material->HasTexture()) {
      glUniform1i(m_Uniforms.use_texture, true);
      glActiveTexture(GL_TEXTURE0);
      material->BindTexture();
      glUniform1i(m_Uniforms.model_texture, 0);
    }

    if (!material_lights) {
      glUniform1f(m_Uniforms.shininess, material->shininess);
      return;
    }
    if (const auto& lighting = material->GetLighting()) {
      LightArrays arrays;
      arrays.Add(static_cast<int>(lighting->type), {lighting->position.x, lighting->position.y, lighting->position.z},
                 {lighting->direction.x, lighting->direction.y, lighting->direction.z},
                 {lighting->color.r, lighting->color.g, lighting->color.b}, lighting->ambient_strength,
                 {lighting->constant, lighting->linear, lighting->quadratic}, {lighting->cutOff, lighting->outerCutOff});
      arrays.Upload(m_Uniforms);
      glUniform1i(m_Uniforms.useLighting, true);
      glUniform1f(m_Uniforms.shininess, lighting->shininess);
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
      glDeleteProgram(m_ShaderID_Shadow);
      DeleteShadowMap();
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

  // Shadows (see DrawShadowMap).
  GLuint m_ShaderID_Shadow = 0;
  struct {
    GLint model = -1;
    GLint lightSpace = -1;
    GLint instanced = -1;
  } shadow_uniforms_;
  GLuint shadow_framebuffer_ = 0;
  GLuint shadow_texture_ = 0;
  int shadow_map_size_ = 0;
  bool shadow_failed_ = false;      // the framebuffer was refused once: don't retry every frame
  bool warned_shadow_type_ = false;
  // This frame's camera, for custom shaders.
  glm::mat4 frame_view_{1.0f};
  glm::mat4 frame_projection_{1.0f};
  glm::vec3 frame_camera_{0.0f};
  std::shared_ptr<scene::Window> main_window_;
  std::unique_ptr<ui::UIRenderer> ui_renderer_;
  RenderDevice* device_;
};

std::unique_ptr<RenderingDriver> RenderingDriver::Create(RenderDevice* device) {
	return std::make_unique<GLRenderingDriver>(device);
}

} // namespace wlw::rendering


#endif // WLW_RENDERER_GL