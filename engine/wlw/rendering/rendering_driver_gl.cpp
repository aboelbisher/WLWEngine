#ifdef WLW_RENDERER_GL

#include <algorithm>
#include <chrono>
#include <cmath>
#include <unordered_map>
#include <vector>

#include <glm/gtc/type_ptr.hpp>

#include "wlw/rendering/gl.h"

#include "wlw/core/logger.h"
#include "wlw/platform/platform.h"
#include "rendering_driver.h"
#include "wlw/rendering/frame_uniforms.h"
#include "wlw/rendering/gl_blend.h"
#include "wlw/rendering/gl_index_buffer.h"
#include "wlw/rendering/gl_instance_buffer.h"
#include "wlw/rendering/gl_program.h"
#include "wlw/rendering/gl_shader.h"
#include "wlw/rendering/gl_vertex_buffer.h"
#include "wlw/rendering/mesh_variant.h"
#include "wlw/rendering/render_device.h"
#include "wlw/rendering/render_queue.h"
#include "wlw/scene/light.h"
#include "wlw/scene/shadow_map.h"
#include "wlw/scene/window.h"
#include "wlw/ui/ui_renderer.h"

namespace wlw::rendering {

// The 3D renderer on GL 3.3 / GLES 3.0. Per viewport:
//   1. gather   visible lights and drawables (opaque / transparent), shadow bounds
//   2. frame    the per-frame uniform block (camera, lights, shadow) uploaded once (frame_uniforms.h)
//   3. shadows  casters' depth from the shadow light into the shadow map
//   4. main     skybox, opaque meshes, then transparent ones back to front; each mesh with the
//               shader variant its features need (mesh_variant.h), or its ShaderMaterial's shader
//   5. canvas   the viewport's 2D canvas on top
class GLRenderingDriver : public RenderingDriver {
public:
  explicit GLRenderingDriver(RenderDevice* device) : device_(device) {}

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

    skybox_program_ = BuildEngineProgram("skybox.vert", "skybox.frag");
    if (skybox_program_ == 0) return false;
    glUseProgram(skybox_program_);
    glUniform1i(glGetUniformLocation(skybox_program_, "uSkybox"), 0);
    glUseProgram(0);

    const float skybox_vertices[] = {
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
    glGenVertexArrays(1, &skybox_vao_);
    glGenBuffers(1, &skybox_vbo_);
    glBindVertexArray(skybox_vao_);
    glBindBuffer(GL_ARRAY_BUFFER, skybox_vbo_);
    glBufferData(GL_ARRAY_BUFFER, sizeof(skybox_vertices), skybox_vertices, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), nullptr);
    glBindVertexArray(0);

    // The per-frame uniform block's buffer: every program reads it at kFrameBlockBinding.
    glGenBuffers(1, &frame_ubo_);
    glBindBuffer(GL_UNIFORM_BUFFER, frame_ubo_);
    glBufferData(GL_UNIFORM_BUFFER, sizeof(FrameUniforms), nullptr, GL_DYNAMIC_DRAW);
    glBindBufferBase(GL_UNIFORM_BUFFER, kFrameBlockBinding, frame_ubo_);

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

  void SetViewport(int x, int y, int width, int height) override { glViewport(x, y, width, height); }

  void Clear() override {
    glClearColor(0.1f, 0.1f, 0.15f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
  }

  // What one viewport draws, gathered from its scene before any drawing starts.
  struct Frame {
    std::vector<const scene::LightNode*> lights;  // visible light nodes (the block takes kMaxLightsPerType of each type)
    std::vector<scene::Node3D*> drawables;         // visible nodes with something to draw (the shadow pass)
    std::vector<scene::Node3D*> opaque;            // ...with an opaque mesh (main pass, first)
    std::vector<scene::Node3D*> transparent;       // ...with a blending mesh (main pass, then, back to front)
    scene::AABB casters{}, receivers{};            // around every shadow caster / receiver
    bool has_casters = false, has_receivers = false;
    const scene::LightNode* shadow_light = nullptr;  // the directional light that casts shadows, if any
    bool shadow_active = false;                      // a shadow map is drawn this frame
    scene::ShadowFrustum shadow_frustum;
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

  void WarmUp(scene::Window& window) override {
    window.MakeContextCurrent();
    const auto start = std::chrono::steady_clock::now();
    const size_t before = mesh_programs_.size();
    // Every node with a mesh counts, visible or not and with or without copies: games build
    // pools of hidden nodes and instanced nodes that fill up later, and those draw soon.
    auto warm = [this](const scene::Viewport& viewport) {
      if (!viewport.GetScene()) return;
      Frame frame;
      bool any_caster = false;
      viewport.GetScene()->IterateOverNodes([&](const std::shared_ptr<scene::Node3D>& node) {
        if (const scene::LightNode* light = node->AsLight()) {
          frame.lights.push_back(light);
          const bool shadows = light->CastsShadows() && light->GetType() == scene::LightNode::Type::Directional;
          if (shadows && frame.shadow_light == nullptr) frame.shadow_light = light;
        } else if (node->GetModel() && !node->GetModel()->meshes.empty()) {
          frame.drawables.push_back(node.get());
          any_caster |= node->CastsShadow();
        }
      });
      frame.shadow_active = frame.shadow_light != nullptr && any_caster;  // as if the map can be made
      for (scene::Node3D* node : frame.drawables) {
        for (const auto& mesh : node->GetModel()->meshes) {
          if (!DrawableShaderMaterial(mesh->GetMaterial()) && !DrawableShaderMaterial(node->GetMaterial())) {
            MeshProgramFor(MeshVariantKey(DrawFor(*node, *mesh, frame)));
          }
        }
        if (frame.shadow_active && node->CastsShadow()) ShadowProgramFor(node->IsInstanced());
      }
    };
    for (const auto& [_, viewport] : window.GetViewports()) warm(*viewport);
    warm(window.GetScreen());
    warmed_up_ = true;
    const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
    WLW_LOG_INFO("Shader warm-up: %zu mesh variant(s) compiled in %.1f ms", mesh_programs_.size() - before, ms);
  }

  // One viewport into the framebuffer that is bound: its scene through its camera, then its
  // canvas over it.
  void DrawViewport(scene::Viewport& viewport, glm::ivec2 pixels, bool has_depth) {
    current_program_ = 0;  // the UI renderer and the debug UI switch programs too: don't trust the cache
    const std::shared_ptr<scene::Scene>& scene = viewport.GetScene();
    const std::shared_ptr<scene::Camera3D>& camera = viewport.GetCamera();
    const bool draw_scene = scene != nullptr && camera != nullptr;

    // Gather, the frame block, and the shadow pass first: the shadow pass draws into its own
    // framebuffer and reads the block's light-space matrix.
    Frame frame;
    if (draw_scene) {
      if (pixels.x > 0 && pixels.y > 0) {  // a minimized window has no size
        camera->UpdateSize({static_cast<float>(pixels.x), static_cast<float>(pixels.y)});
      }
      GatherScene(*scene, frame);
      PrepareShadow(frame);
      UploadFrame(*camera, frame);
      if (frame.shadow_active) DrawShadowMap(frame);
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
    if (draw_scene) DrawMainPass(*scene, frame, stats);
    viewport.SetRenderStats(stats);

    ui_renderer_->Draw(viewport.GetCanvas(), {static_cast<float>(pixels.x), static_cast<float>(pixels.y)});
  }

  // --- 1. GATHER: lights, things to draw, and the bounds of everything shadows involve ---
  void GatherScene(const scene::Scene& scene, Frame& frame) {
    scene.IterateOverNodes([&frame](const std::shared_ptr<scene::Node3D>& node) {
      if (!node->IsVisibleInHierarchy()) return;
      if (const scene::LightNode* light = node->AsLight()) {
        frame.lights.push_back(light);
        return;
      }
      auto model = node->GetModel();
      if (model == nullptr || model->meshes.empty()) return;
      if (node->IsInstanced() && node->GetInstances().empty()) return;  // instanced with zero copies
      frame.drawables.push_back(node.get());
      bool has_opaque = false, has_transparent = false;
      for (const auto& mesh : model->meshes) {
        const bool opaque = QueueFor(EffectiveMaterialBlend(*node, *mesh)) == RenderQueue::Opaque;
        has_opaque |= opaque;
        has_transparent |= !opaque;
      }
      if (has_opaque) frame.opaque.push_back(node.get());
      if (has_transparent) frame.transparent.push_back(node.get());
      if (node->CastsShadow()) Grow(frame.casters, frame.has_casters, node->GetAABB());
      if (node->ReceivesShadow()) Grow(frame.receivers, frame.has_receivers, node->GetAABB());
    });

    // The shadow light: the first directional light that casts shadows.
    for (const scene::LightNode* light : frame.lights) {
      if (!light->CastsShadows()) continue;
      if (light->GetType() != scene::LightNode::Type::Directional) {
        if (!warned_shadow_type_) WLW_LOG_WARN("Only directional lights cast shadows (for now): ignoring the others");
        warned_shadow_type_ = true;
        continue;
      }
      if (frame.shadow_light == nullptr) frame.shadow_light = light;
    }
  }

  // The shadow light's box around the casters, and the shadow map to draw it into.
  void PrepareShadow(Frame& frame) {
    if (frame.shadow_light == nullptr || !frame.has_casters) return;
    const scene::LightNode& light = *frame.shadow_light;
    if (!EnsureShadowMap(light.shadow.map_size)) return;
    frame.shadow_frustum = scene::FitDirectionalShadow(light.GetDirection(), frame.casters,
                                                       frame.has_receivers ? frame.receivers : frame.casters,
                                                       shadow_map_size_);
    frame.shadow_active = true;
  }

  // --- 2. FRAME: the per-frame uniform block, read by every program this viewport draws with ---
  void UploadFrame(scene::Camera3D& camera, const Frame& frame) {
    FrameUniforms block;
    block.view = camera.GetViewMatrix();
    block.projection = camera.GetProjectionMatrix();
    block.view_projection = block.projection * block.view;
    const core::Vector3 position = camera.GetPosition();
    // The projection tells the two kinds apart: w is 1 for orthographic (no divide by depth), -z for
    // perspective. Row 2 of the view matrix is the camera's backward axis in world space.
    block.camera_position = {position.x, position.y, position.z, block.projection[3][3] == 1.0f ? 1.0f : 0.0f};
    block.view_forward = {-block.view[0][2], -block.view[1][2], -block.view[2][2], 0.0f};
    if (frame.shadow_active) {
      const scene::ShadowSettings& settings = frame.shadow_light->shadow;
      block.light_space = frame.shadow_frustum.ViewProjection();
      block.view_forward.w = settings.strength;
      block.shadow_params = {settings.softness, 1.0f / static_cast<float>(shadow_map_size_),
                             frame.shadow_frustum.texel_world * 1.5f, 0.0f};
    }
    SetFrameLights(block, frame.lights, frame.shadow_active ? frame.shadow_light : nullptr);
    scene_view_ = block.view;
    scene_view_projection_ = block.view_projection;

    glBindBuffer(GL_UNIFORM_BUFFER, frame_ubo_);
    glBufferSubData(GL_UNIFORM_BUFFER, 0, sizeof(FrameUniforms), &block);
    glBindBufferBase(GL_UNIFORM_BUFFER, kFrameBlockBinding, frame_ubo_);
  }

  // --- 3. SHADOWS: every caster's depth, seen from the shadow light ---
  void DrawShadowMap(const Frame& frame) {
    GLint previous_framebuffer = 0;  // not always 0: the platform may draw into its own
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &previous_framebuffer);
    glBindFramebuffer(GL_FRAMEBUFFER, shadow_framebuffer_);
    glViewport(0, 0, shadow_map_size_, shadow_map_size_);
    glDepthMask(GL_TRUE);
    glClear(GL_DEPTH_BUFFER_BIT);
    // A depth bias that grows with the surface's slope to the light: against "shadow acne".
    glEnable(GL_POLYGON_OFFSET_FILL);
    glPolygonOffset(1.5f, 3.0f);
    for (scene::Node3D* node : frame.drawables) {
      if (!node->CastsShadow()) continue;
      const ShadowProgram& program = ShadowProgramFor(node->IsInstanced());
      if (program.id == 0) continue;
      UseProgram(program.id);
      glUniformMatrix4fv(program.model, 1, GL_FALSE, glm::value_ptr(node->GetModelMatrix()));
      GLInstanceBuffer* instances = PrepareInstances(*node);
      for (const auto& mesh : node->GetModel()->meshes) DrawMesh(*node, *mesh, instances, nullptr);
    }
    glDisable(GL_POLYGON_OFFSET_FILL);
    glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(previous_framebuffer));
  }

  // --- 4. MAIN PASS: skybox, opaque meshes, then transparent ones back to front ---
  void DrawMainPass(const scene::Scene& scene, const Frame& frame, scene::RenderStats& stats) {
    if (const auto& skybox = scene.GetSkybox()) {
      glDisable(GL_DEPTH_TEST);
      glDepthMask(GL_FALSE);
      glDisable(GL_CULL_FACE);
      UseProgram(skybox_program_);
      glBindVertexArray(skybox_vao_);
      skybox->Bind(0);
      glDrawArrays(GL_TRIANGLES, 0, 36);
      glBindVertexArray(0);
      glDepthMask(GL_TRUE);
      glEnable(GL_DEPTH_TEST);
    }

    // The shadow map on texture unit 1 (unit 0: the mesh's texture; units 2+: ShaderMaterials').
    if (frame.shadow_active) {
      glActiveTexture(GL_TEXTURE1);
      glBindTexture(GL_TEXTURE_2D, shadow_texture_);
      glActiveTexture(GL_TEXTURE0);
    }

    const glm::mat4& view = scene_view_;
    const scene::Frustum frustum = scene::Frustum::FromMatrix(scene_view_projection_);

    // Opaque first: any order, the depth buffer sorts them per pixel.
    for (scene::Node3D* node : frame.opaque) {
      if (!frustum.TestAABB(node->GetAABB())) {
        ++stats.culled_nodes;
        continue;
      }
      DrawNode(*node, frame, RenderQueue::Opaque, stats);
    }

    // Then whatever blends: farthest first, so each one blends over what's behind it.
    std::vector<scene::Node3D*> transparent;
    transparent.reserve(frame.transparent.size());
    for (scene::Node3D* node : frame.transparent) {
      if (frustum.TestAABB(node->GetAABB())) {
        transparent.push_back(node);
      } else if (std::find(frame.opaque.begin(), frame.opaque.end(), node) == frame.opaque.end()) {
        ++stats.culled_nodes;  // not counted twice for a node with meshes in both queues
      }
    }
    SortBackToFront(transparent, view, [](const scene::Node3D* node) {
      const scene::AABB box = node->GetAABB();
      return glm::vec3((box.min.x + box.max.x) * 0.5f, (box.min.y + box.max.y) * 0.5f, (box.min.z + box.max.z) * 0.5f);
    });
    for (scene::Node3D* node : transparent) DrawNode(*node, frame, RenderQueue::Transparent, stats);

    ApplyBlendMode(BlendMode::Opaque);  // back to the defaults for whatever draws next
    glDepthMask(GL_TRUE);
    UseProgram(0);
  }

  // A node's meshes that belong to `queue`, each with its material's blend state and shader: its
  // ShaderMaterial's, else the engine's variant for its features.
  void DrawNode(scene::Node3D& node, const Frame& frame, RenderQueue queue, scene::RenderStats& stats) {
    const glm::mat4& model = node.GetModelMatrix();
    const bool instanced = node.IsInstanced();
    // Normals: transpose(inverse(model)), once per node on the CPU (instanced copies do it per vertex).
    const glm::mat3 normal_matrix = instanced ? glm::mat3(1.0f) : glm::mat3(glm::transpose(glm::inverse(model)));
    GLInstanceBuffer* instances = PrepareInstances(node);

    for (const auto& mesh : node.GetModel()->meshes) {
      const BlendMode blend = EffectiveMaterialBlend(node, *mesh);
      if (QueueFor(blend) != queue) continue;  // drawn in the other queue
      ApplyBlendMode(blend);
      glDepthMask(EffectiveDepthWrite(node, *mesh) ? GL_TRUE : GL_FALSE);
      if (queue == RenderQueue::Transparent) ++stats.transparent_draw_calls;

      const ShaderMaterial* custom = DrawableShaderMaterial(mesh->GetMaterial());
      if (!custom) custom = DrawableShaderMaterial(node.GetMaterial());
      if (custom) {
        UseShaderMaterial(*custom, model, instanced);
      } else {
        const MeshProgram& program = MeshProgramFor(MeshVariantKey(DrawFor(node, *mesh, frame)));
        if (program.id == 0) continue;  // didn't compile (logged)
        UseProgram(program.id);
        glUniformMatrix4fv(program.model, 1, GL_FALSE, glm::value_ptr(model));
        if (!instanced) glUniformMatrix3fv(program.normal_matrix, 1, GL_FALSE, glm::value_ptr(normal_matrix));
        const Material* material = EffectiveMaterial(node, *mesh);
        glUniform1f(program.shininess, material ? material->shininess : 32.0f);
        if (const Material* textured = TexturedMaterial(node, *mesh)) {
          glActiveTexture(GL_TEXTURE0);
          textured->BindTexture();
        }
      }
      DrawMesh(node, *mesh, instances, &stats);
    }
  }

  // The draw's features, which pick its shader variant.
  template <typename MeshT>
  static MeshDraw DrawFor(const scene::Node3D& node, const MeshT& mesh, const Frame& frame) {
    MeshDraw draw;
    draw.instanced = node.IsInstanced();
    draw.textured = TexturedMaterial(node, mesh) != nullptr;
    draw.scene_lit = !frame.lights.empty();
    draw.shadow_active = frame.shadow_active;
    draw.receives_shadow = node.ReceivesShadow();
    draw.blends = EffectiveMaterialBlend(node, mesh) != BlendMode::Opaque;
    return draw;
  }

  // The material that decides a mesh's look: its own, else its node's (the mesh's wins).
  template <typename MeshT>
  static const Material* EffectiveMaterial(const scene::Node3D& node, const MeshT& mesh) {
    if (const auto& material = mesh.GetMaterial()) return material.get();
    return node.GetMaterial().get();
  }
  // The material whose texture the mesh shows: its own if it has one, else its node's.
  template <typename MeshT>
  static const Material* TexturedMaterial(const scene::Node3D& node, const MeshT& mesh) {
    if (const auto& material = mesh.GetMaterial(); material && material->HasTexture()) return material.get();
    if (const auto& material = node.GetMaterial(); material && material->HasTexture()) return material.get();
    return nullptr;
  }
  template <typename MeshT>
  static BlendMode EffectiveMaterialBlend(const scene::Node3D& node, const MeshT& mesh) {
    const Material* material = EffectiveMaterial(node, mesh);
    return material ? material->blend : BlendMode::Opaque;
  }
  template <typename MeshT>
  static bool EffectiveDepthWrite(const scene::Node3D& node, const MeshT& mesh) {
    const Material* material = EffectiveMaterial(node, mesh);
    return material ? material->DepthWrite() : true;
  }

  // An instanced node's per-copy data on the GPU (uploaded only when it changed: Upload compares
  // versions); null for a plain node.
  static GLInstanceBuffer* PrepareInstances(scene::Node3D& node) {
    if (!node.IsInstanced()) return nullptr;
    if (!node.GetInstanceBuffer()) node.SetInstanceBuffer(std::make_unique<GLInstanceBuffer>());
    auto* buffer = static_cast<GLInstanceBuffer*>(node.GetInstanceBuffer());
    buffer->Upload(node.GetInstances(), node.GetInstancesVersion());
    return buffer;
  }

  // Draws one mesh with whatever program is in use: once, or once per copy (one call either way).
  template <typename MeshT>
  void DrawMesh(scene::Node3D& node, MeshT& mesh, GLInstanceBuffer* instances, scene::RenderStats* stats) {
    if (!mesh.GetVertexBuffer()) mesh.SetVertexBuffer(device_->CreateVertexBuffer(mesh.GetVertices()));
    if (!mesh.GetIndexBuffer()) mesh.SetIndexBuffer(device_->CreateIndexBuffer(mesh.GetIndices()));
    const auto* vertex_buffer = mesh.GetVertexBuffer();
    const auto* index_buffer = mesh.GetIndexBuffer();
    vertex_buffer->Bind();
    index_buffer->Bind();
    const auto index_count = static_cast<GLsizei>(mesh.GetIndices().size());
    if (instances) {
      const auto count = static_cast<GLsizei>(node.GetInstances().size());
      instances->Attach();
      glDrawElementsInstanced(GL_TRIANGLES, index_count, GL_UNSIGNED_INT, nullptr, count);
      GLInstanceBuffer::Detach();
      if (stats) stats->instances += count;
    } else {
      glDrawElements(GL_TRIANGLES, index_count, GL_UNSIGNED_INT, nullptr);
    }
    if (stats) ++stats->draw_calls;
    vertex_buffer->Unbind();
    index_buffer->Unbind();
  }

  // The material as a ShaderMaterial with a working 3D shader, else null (an invalid one falls
  // back to the engine's shader).
  static const ShaderMaterial* DrawableShaderMaterial(const std::shared_ptr<rendering::Material>& material) {
    const ShaderMaterial* shader_material = material ? material->AsShaderMaterial() : nullptr;
    if (!shader_material || !shader_material->IsValid()) return nullptr;
    if (shader_material->GetShader()->GetTarget() != Shader::Target::Mesh) return nullptr;
    return shader_material;
  }

  // A ShaderMaterial's shader with its model matrix and params (the camera and lights come from
  // the frame block). Its textures start at unit 2: units 0-1 are the mesh texture and the
  // shadow map, which stay bound for the nodes drawn after.
  void UseShaderMaterial(const ShaderMaterial& material, const glm::mat4& model, bool instanced) {
    const auto& shader = static_cast<const GLShader&>(*material.GetShader());
    UseProgram(shader.Program());
    glUniformMatrix4fv(shader.Location("uModel"), 1, GL_FALSE, glm::value_ptr(model));
    glUniform1i(shader.Location("uInstanced"), instanced);
    shader.ApplyParams(material, 2);
  }

  // ---- Programs ----
  struct MeshProgram {
    GLuint id = 0;
    GLint model = -1;
    GLint normal_matrix = -1;
    GLint shininess = -1;
  };
  struct ShadowProgram {
    GLuint id = 0;
    GLint model = -1;
  };

  // The engine's 3D shader for a variant key: compiled the first time it's needed, then cached.
  const MeshProgram& MeshProgramFor(uint32_t key) {
    auto [it, inserted] = mesh_programs_.try_emplace(key);
    if (!inserted) return it->second;
    const auto start = std::chrono::steady_clock::now();
    MeshProgram& program = it->second;
    program.id = BuildEngineProgram("mesh.vert", "mesh.frag", MeshVariantDefines(key));
    if (program.id == 0) return program;
    program.model = glGetUniformLocation(program.id, "uModel");
    program.normal_matrix = glGetUniformLocation(program.id, "uNormalMatrix");
    program.shininess = glGetUniformLocation(program.id, "uShininess");
    // Two sampler types may never share a texture unit: the mesh texture uses 0, shadows 1.
    UseProgram(program.id);
    glUniform1i(glGetUniformLocation(program.id, "uTexture"), 0);
    glUniform1i(glGetUniformLocation(program.id, "uShadowMap"), 1);
    if (warmed_up_) {  // not prepared by WarmUp: it costs a frame now (worth knowing on phones)
      const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
      WLW_LOG_INFO("Shader variant compiled during play (%.1f ms):%s", ms, VariantName(key).c_str());
    }
    return program;
  }

  const ShadowProgram& ShadowProgramFor(bool instanced) {
    ShadowProgram& program = shadow_programs_[instanced ? 1 : 0];
    if (program.id != 0) return program;
    program.id = BuildEngineProgram("shadow.vert", "shadow.frag", MeshVariantDefines(instanced ? kMeshInstanced : 0));
    if (program.id != 0) program.model = glGetUniformLocation(program.id, "uModel");
    return program;
  }

  static std::string VariantName(uint32_t key) {
    std::string name;
    for (const std::string& define : MeshVariantDefines(key)) name += " " + define;
    return name.empty() ? " (none)" : name;
  }

  void UseProgram(GLuint program) {
    if (program == current_program_) return;
    glUseProgram(program);
    current_program_ = program;
  }

  // ---- Shadow map ----
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

  static void Grow(scene::AABB& box, bool& has_box, const scene::AABB& add) {
    if (!has_box) {
      box = add;
      has_box = true;
      return;
    }
    box.min = {std::min(box.min.x, add.min.x), std::min(box.min.y, add.min.y), std::min(box.min.z, add.min.z)};
    box.max = {std::max(box.max.x, add.max.x), std::max(box.max.y, add.max.y), std::max(box.max.z, add.max.z)};
  }

  ~GLRenderingDriver() override {
    if (!platform::IsAlive()) return;
    // Free our GL objects while the main window's context still exists...
    if (main_window_) {
      main_window_->MakeContextCurrent();
      ui_renderer_.reset();
      for (const auto& [_, program] : mesh_programs_) {
        if (program.id) glDeleteProgram(program.id);
      }
      for (const ShadowProgram& program : shadow_programs_) {
        if (program.id) glDeleteProgram(program.id);
      }
      if (skybox_program_) glDeleteProgram(skybox_program_);
      if (frame_ubo_) glDeleteBuffers(1, &frame_ubo_);
      DeleteShadowMap();
      glDeleteVertexArrays(1, &skybox_vao_);
      glDeleteBuffers(1, &skybox_vbo_);
    }
    main_window_.reset();
    // ...then shut the platform down (destroys any windows / contexts still open).
    platform::Shutdown();
  }

private:
  RenderDevice* device_;
  std::shared_ptr<scene::Window> main_window_;
  std::unique_ptr<ui::UIRenderer> ui_renderer_;

  std::unordered_map<uint32_t, MeshProgram> mesh_programs_;  // by variant key
  ShadowProgram shadow_programs_[2];                          // plain, instanced
  GLuint skybox_program_ = 0;
  GLuint skybox_vao_ = 0;
  GLuint skybox_vbo_ = 0;
  GLuint frame_ubo_ = 0;
  GLuint current_program_ = 0;  // what glUseProgram last set (to skip redundant switches)
  bool warmed_up_ = false;

  // This viewport's camera (set by UploadFrame), for culling and sorting.
  glm::mat4 scene_view_{1.0f};
  glm::mat4 scene_view_projection_{1.0f};

  // Shadows (see DrawShadowMap).
  GLuint shadow_framebuffer_ = 0;
  GLuint shadow_texture_ = 0;
  int shadow_map_size_ = 0;
  bool shadow_failed_ = false;      // the framebuffer was refused once: don't retry every frame
  bool warned_shadow_type_ = false;
};

std::unique_ptr<RenderingDriver> RenderingDriver::Create(RenderDevice* device) {
  return std::make_unique<GLRenderingDriver>(device);
}

}  // namespace wlw::rendering

#endif  // WLW_RENDERER_GL
