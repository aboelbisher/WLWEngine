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
#include "wlw/rendering/gl_gpu_queries.h"
#include "wlw/rendering/gl_index_buffer.h"
#include "wlw/rendering/gl_instance_buffer.h"
#include "wlw/rendering/gl_program.h"
#include "wlw/rendering/gl_resources.h"
#include "wlw/rendering/gl_shader.h"
#include "wlw/rendering/gl_vertex_buffer.h"
#include "wlw/rendering/mesh_variant.h"
#include "wlw/rendering/post_pass.h"
#include "wlw/rendering/shader_library.h"
#include "wlw/rendering/render_device.h"
#include "wlw/rendering/render_list.h"
#include "wlw/rendering/gl_state_cache.h"
#include "wlw/scene/light.h"
#include "wlw/scene/shadow_map.h"
#include "wlw/scene/sprite_batch.h"
#include "wlw/scene/window.h"
#include "wlw/ui/ui_renderer.h"

namespace wlw::rendering {

// The 3D renderer on GL 3.3 / GLES 3.0. Per viewport:
//   1. gather   visible lights and drawables (each one's world box computed once), shadow bounds
//   2. frame    the per-frame uniform block (camera, lights, shadow) uploaded once (frame_uniforms.h)
//   3. shadows  casters' depth from the shadow light into the shadow map
//   4. main     skybox, opaque meshes, then transparent ones back to front; each mesh with the
//               shader variant its features need (mesh_variant.h), or its ShaderMaterial's shader
//   5. 2D        its Picture canvas layers (part of the picture)
//   6. post      its post passes, if any (steps 3-5 then drew into the scene picture): full-screen
//                shaders, the last one writing the viewport's picture (post_pass.h)
//   7. 2D        its Overlay canvas layers on top (its default canvas = the game UI on the screen)
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
    glGenVertexArrays(1, &post_vao_);  // GL 3.3 core draws need a vertex array, even an empty one
    CopyShader();                      // made now, so the warm-up compiles it

    // The per-frame uniform block's buffer: every program reads it at kFrameBlockBinding.
    glGenBuffers(1, &frame_ubo_);
    glBindBuffer(GL_UNIFORM_BUFFER, frame_ubo_);
    glBufferData(GL_UNIFORM_BUFFER, sizeof(FrameUniforms), nullptr, GL_DYNAMIC_DRAW);
    glBindBufferBase(GL_UNIFORM_BUFFER, kFrameBlockBinding, frame_ubo_);

    // Draws each viewport's 2D canvas (the screen's is the game UI).
    ui_renderer_ = ui::UIRenderer::Create();

    // GPU timing, if this GPU can (gpu_timer.h).
    if (auto queries = CreateGLGpuQueries()) gpu_timer_ = std::make_unique<GpuTimer>(std::move(queries));
    window->SetGpuTimingSupported(gpu_timer_ != nullptr);

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
    struct Drawable {
      scene::Node3D* node;
      scene::AABB box;  // world bounds, computed once per frame (GetAABB walks every mesh / copy)
    };
    std::vector<Drawable> drawables;               // visible nodes with something to draw
    struct Sprites {
      scene::SpriteBatch* batch;
      scene::AABB box;  // world bounds (its SetBounds box through its transform)
    };
    std::vector<Sprites> sprites;                  // visible sprite batches with dots (no shadows)
    scene::AABB casters{}, receivers{};            // around every shadow caster / receiver
    bool has_casters = false, has_receivers = false;
    const scene::LightNode* shadow_light = nullptr;  // the directional light that casts shadows, if any
    bool shadow_active = false;                      // a shadow map is drawn this frame
    scene::ShadowFrustum shadow_frustum;

    // Empties it for the next viewport, keeping the lists' memory (no allocations per frame).
    void Clear() {
      lights.clear();
      drawables.clear();
      sprites.clear();
      has_casters = has_receivers = false;
      shadow_light = nullptr;
      shadow_active = false;
    }
  };

  // One frame of a window: its offscreen viewports into their textures (in the order added, so
  // one may use the picture of an earlier one), then the screen, the overlays, and present.
  void DrawWindow(std::shared_ptr<scene::Window> window) override {
    window->MakeContextCurrent();

    GLint screen_framebuffer = 0;  // not always 0: the platform may draw into its own
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &screen_framebuffer);
    const glm::vec2 screen_pixels{window->GetSize().x, window->GetSize().y};

    // GPU timing: this frame's sections are measured; the results of one a few frames ago arrive.
    timing_ = gpu_timer_ && window->GpuTiming();
    if (timing_) {
      gpu_timer_->BeginFrame(gpu_samples_);
      ApplyGpuSamples(*window);
    }

    for (const auto& [_, viewport] : window->GetViewports()) {
      RenderTarget* target = viewport->GetTarget();
      if (target == nullptr) continue;  // only the window's own GetScreen() draws to the screen
      const glm::ivec2 pixels = viewport->PixelSize(screen_pixels);
      if (!viewport->BeginDraw(pixels)) continue;
      target->SetSize(pixels.x, pixels.y);
      DrawViewport(*viewport, pixels, GLFramebuffer(*target), target->Settings().depth);
    }

    DrawViewport(window->GetScreen(), {static_cast<int>(screen_pixels.x), static_cast<int>(screen_pixels.y)},
                 static_cast<GLuint>(screen_framebuffer), /*has_depth=*/true);
    if (timing_) gpu_timer_->End();
    window->DrawOverlays();  // e.g. the debug UI: on top of everything (not timed)

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
        if (const scene::SpriteBatch* sprites = node->AsSpriteBatch()) {
          SpriteProgramFor(SpriteTexture(*sprites) != nullptr);
          return;
        }
        if (const scene::LightNode* light = node->AsLight()) {
          frame.lights.push_back(light);
          const bool shadows = light->CastsShadows() && light->GetType() == scene::LightNode::Type::Directional;
          if (shadows && frame.shadow_light == nullptr) frame.shadow_light = light;
        } else if (node->GetModel() && !node->GetModel()->meshes.empty()) {
          frame.drawables.push_back({node.get(), {}});
          any_caster |= node->CastsShadow();
        }
      });
      frame.shadow_active = frame.shadow_light != nullptr && any_caster;  // as if the map can be made
      for (const Frame::Drawable& drawable : frame.drawables) {
        scene::Node3D* node = drawable.node;
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
    // Every game shader and texture alive now (canvas shaders too: no scene says they're needed).
    const int custom = PrepareAliveResources();
    warmed_up_ = true;
    const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
    WLW_LOG_INFO("Shader warm-up: %zu mesh variant(s) + %d game shader(s) compiled in %.1f ms",
                 mesh_programs_.size() - before, custom, ms);
  }

  // One viewport into `destination` (the screen's framebuffer or its target's): its scene through
  // its camera, its post passes if it has any (the scene then goes into the scene picture first,
  // and the last pass writes `destination`), then its canvas over it.
  void DrawViewport(scene::Viewport& viewport, glm::ivec2 pixels, GLuint destination, bool has_depth) {
    using Clock = std::chrono::steady_clock;
    // With post passes the 3D goes into the scene picture, a target of the viewport's own format.
    const PostPlan* post = nullptr;
    GLuint scene_framebuffer = destination;
    if (!viewport.Post().Empty() && pixels.x > 0 && pixels.y > 0) {
      const scene::ViewportSettings& settings = viewport.Settings();
      post = &viewport.Post().Prepare(pixels, {settings.format, settings.smooth, settings.depth});
      scene_framebuffer = GLFramebuffer(viewport.Post().SceneTarget());
      has_depth = settings.depth;
    }
    const std::shared_ptr<scene::Scene>& scene = viewport.GetScene();
    const std::shared_ptr<scene::Camera3D>& camera = viewport.GetCamera();
    const bool draw_scene = scene != nullptr && camera != nullptr;
    state_.Reset();  // the UI renderer and the debug UI change GL state too: don't trust the cache
    state_.ResetCounters();
    scene::RenderStats stats;

    // Gather, the frame block and the draw list first, then the shadow pass: it draws into its own
    // framebuffer and reads the block's light-space matrix.
    frame_.Clear();
    draw_items_.clear();
    draw_order_.clear();
    node_slots_.clear();
    const auto start = Clock::now();
    if (draw_scene) {
      if (pixels.x > 0 && pixels.y > 0) {  // a minimized window has no size
        camera->UpdateSize({static_cast<float>(pixels.x), static_cast<float>(pixels.y)});
      }
      GatherScene(*scene, frame_);
      PrepareShadow(frame_);
      SetCameraMatrices(*camera);
      BuildDrawList(frame_, stats);
    }
    const auto gathered = Clock::now();
    SortDrawOrder(draw_order_);
    const auto sorted = Clock::now();
    // GPU work from here on: the frame block, instance data, the shadow and main passes.
    glBindFramebuffer(GL_FRAMEBUFFER, scene_framebuffer);  // (the shadow pass returns to it)
    TimeSection(viewport.Id(), GpuSection::Kind::Scene);
    if (draw_scene) {
      UploadFrame(*camera, frame_);
      for (NodeSlot& slot : node_slots_) slot.instances = PrepareInstances(*slot.node, &stats);
      if (frame_.shadow_active) DrawShadowMap(frame_);
    }

    SetViewport(0, 0, pixels.x, pixels.y);
    state_.SetDepthWrite(true);
    GLbitfield clear_bits = has_depth ? GL_DEPTH_BUFFER_BIT : 0;
    if (const auto& color = viewport.GetClearColor()) {
      glClearColor(color->r, color->g, color->b, color->a);
      clear_bits |= GL_COLOR_BUFFER_BIT;
    }
    if (clear_bits) glClear(clear_bits);

    if (draw_scene) DrawMainPass(*scene, frame_, stats);
    const auto submitted = Clock::now();

    auto micros = [](Clock::duration d) { return std::chrono::duration<float, std::micro>(d).count(); };
    stats.gather_us = micros(gathered - start);
    stats.sort_us = micros(sorted - gathered);
    stats.submit_us = micros(submitted - sorted);
    stats.program_changes = state_.ProgramChanges();
    stats.texture_binds = state_.TextureBinds();
    stats.state_calls_skipped = state_.Skipped();

    // 2D: Picture layers into the picture (before the passes), Overlay layers on top after them.
    const glm::vec2 canvas_pixels{static_cast<float>(pixels.x), static_cast<float>(pixels.y)};
    auto draw_layers = [&](scene::CanvasMode mode) {
      bool timed = false;
      viewport.ForEachCanvasLayer(mode, [&](const ui::Canvas& canvas) {
        if (canvas.QuadCount() == 0) return;
        if (!timed) TimeSection(viewport.Id(), GpuSection::Kind::Canvas);
        timed = true;
        ui_renderer_->Draw(canvas, canvas_pixels);
      });
      if (timed && timing_) gpu_timer_->End();
    };
    draw_layers(scene::CanvasMode::Picture);
    state_.Reset();  // the UI renderer changed program, blend and textures behind the cache's back

    if (post) {
      const auto post_start = Clock::now();
      DrawPostPasses(viewport.Post(), *post, destination, viewport.Id());
      stats.post_passes = static_cast<int>(post->steps.size());
      stats.post_targets = viewport.Post().TargetCount();
      stats.post_us = micros(Clock::now() - post_start);
    }
    viewport.SetRenderStats(stats);

    draw_layers(scene::CanvasMode::Overlay);
  }

  // --- GPU TIMING (gpu_timer.h): sections measured one after another, results frames later ---
  void TimeSection(uint32_t viewport, GpuSection::Kind kind, int index = 0) {
    if (timing_) gpu_timer_->Begin({viewport, kind, index});
  }

  // A frame's results (from a few frames ago) into each viewport's GpuStats and the window's
  // frame time. A viewport that wasn't drawn then keeps its numbers; one that's gone is skipped.
  void ApplyGpuSamples(scene::Window& window) {
    if (gpu_samples_.empty()) return;
    float frame_ms = 0.0f;
    for (const GpuSample& sample : gpu_samples_) frame_ms += sample.ms;
    window.SetGpuFrameMs(SmoothMs(window.GpuFrameMs(), frame_ms));
    auto apply = [this](scene::Viewport& viewport) {
      float scene_ms = -1.0f, post_ms = -1.0f, canvas_ms = -1.0f;
      raw_pass_ms_.clear();
      for (const GpuSample& sample : gpu_samples_) {
        if (sample.section.viewport != viewport.Id()) continue;
        auto add = [&sample](float& total) { total = std::max(total, 0.0f) + sample.ms; };
        switch (sample.section.kind) {
          case GpuSection::Kind::Scene: add(scene_ms); break;
          case GpuSection::Kind::Canvas: add(canvas_ms); break;
          case GpuSection::Kind::PostPass: {
            add(post_ms);
            const auto index = static_cast<size_t>(sample.section.index);
            if (raw_pass_ms_.size() <= index) raw_pass_ms_.resize(index + 1, 0.0f);
            raw_pass_ms_[index] = sample.ms;
            break;
          }
        }
      }
      if (scene_ms < 0.0f && canvas_ms < 0.0f) return;  // not drawn that frame
      scene::GpuStats& stats = viewport.MutableGpuStats();
      stats.scene_ms = scene_ms < 0.0f ? -1.0f : SmoothMs(stats.scene_ms, scene_ms);
      stats.canvas_ms = canvas_ms < 0.0f ? -1.0f : SmoothMs(stats.canvas_ms, canvas_ms);
      stats.post_ms = post_ms < 0.0f ? -1.0f : SmoothMs(stats.post_ms, post_ms);
      stats.pass_ms.resize(raw_pass_ms_.size(), -1.0f);
      for (size_t i = 0; i < raw_pass_ms_.size(); ++i) stats.pass_ms[i] = SmoothMs(stats.pass_ms[i], raw_pass_ms_[i]);
    };
    for (const auto& [_, viewport] : window.GetViewports()) apply(*viewport);
    apply(window.GetScreen());
  }

  // --- POST PASSES: full-screen shaders from the scene picture to `destination` (post_pass.h) ---
  void DrawPostPasses(PostChain& chain, const PostPlan& plan, GLuint destination, uint32_t viewport_id) {
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    state_.SetBlendMode(BlendMode::Opaque);  // each pass replaces its output's pixels
    glBindVertexArray(post_vao_);            // no attributes: post.vert makes the triangle from gl_VertexID
    const std::vector<PostPass>& passes = chain.Passes();
    for (const PostStep& step : plan.steps) {
      const PostPass& pass = passes[static_cast<size_t>(step.pass)];
      TimeSection(viewport_id, GpuSection::Kind::PostPass, step.pass);
      const GLuint output = step.output == kDestination ? destination : GLFramebuffer(chain.Target(step.output));
      glBindFramebuffer(GL_FRAMEBUFFER, output);
      glViewport(0, 0, step.size.x, step.size.y);

      // The pass's shader; one that's missing, of another kind or not compiling copies instead.
      const ShaderMaterial* material = pass.material.get();
      const GLShader* shader = nullptr;
      if (material && material->IsValid() && material->GetShader()->GetTarget() == Shader::Target::Post) {
        shader = GLShaderFor(*material->GetShader());
      }
      if (!shader) {
        material = nullptr;
        shader = GLShaderFor(*CopyShader());
        if (!shader) continue;
      }
      state_.UseProgram(shader->Program());

      const WTexture2D& source = *chain.TextureOf(step.source);
      BindGLTexture(source, 0);
      glUniform1i(shader->Location("uSource"), 0);
      BindGLTexture(*chain.TextureOf(kSceneTarget), 1);
      glUniform1i(shader->Location("uScene"), 1);
      glUniform2f(shader->Location("uTexel"), 1.0f / static_cast<float>(source.Width()),
                  1.0f / static_cast<float>(source.Height()));
      glUniform2f(shader->Location("uOutputSize"), static_cast<float>(step.size.x), static_cast<float>(step.size.y));
      int unit = 2;
      for (const auto& [uniform, texture] : step.inputs) {
        BindGLTexture(*chain.TextureOf(texture), static_cast<unsigned int>(unit));
        glUniform1i(shader->Location(uniform), unit);
        ++unit;
      }
      if (material) shader->ApplyParams(*material, unit);
      glDrawArrays(GL_TRIANGLES, 0, 3);
    }
    glBindVertexArray(0);
    glActiveTexture(GL_TEXTURE0);
    state_.ForgetTexture0();
    glEnable(GL_DEPTH_TEST);
    glBindFramebuffer(GL_FRAMEBUFFER, destination);  // the canvas draws there next
  }

  // What a pass without a usable shader does: copies uSource.
  const std::shared_ptr<Shader>& CopyShader() {
    if (!copy_shader_) {
      const std::string* text = FindShaderFile("post/copy.glsl");
      copy_shader_ = Shader::Create(text ? *text : std::string(), "post/copy.glsl");
    }
    return copy_shader_;
  }

  // --- 1. GATHER: lights, things to draw, and the bounds of everything shadows involve ---
  void GatherScene(const scene::Scene& scene, Frame& frame) {
    scene.IterateOverNodes([&frame](const std::shared_ptr<scene::Node3D>& node) {
      if (!node->IsVisibleInHierarchy()) return;
      if (scene::SpriteBatch* sprites = node->AsSpriteBatch()) {
        if (!sprites->Empty()) frame.sprites.push_back({sprites, sprites->WorldBounds()});
        return;
      }
      if (const scene::LightNode* light = node->AsLight()) {
        frame.lights.push_back(light);
        return;
      }
      auto model = node->GetModel();
      if (model == nullptr || model->meshes.empty()) return;
      if (node->IsInstanced() && node->GetInstances().empty()) return;  // instanced with zero copies
      const scene::AABB box = node->GetAABB();
      frame.drawables.push_back({node.get(), box});
      if (node->CastsShadow()) Grow(frame.casters, frame.has_casters, box);
      if (node->ReceivesShadow()) Grow(frame.receivers, frame.has_receivers, box);
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

  // This viewport's camera, for culling and depth (BuildDrawList): no GL.
  void SetCameraMatrices(scene::Camera3D& camera) {
    scene_view_ = camera.GetViewMatrix();
    scene_view_projection_ = camera.GetProjectionMatrix() * scene_view_;
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

    glBindBuffer(GL_UNIFORM_BUFFER, frame_ubo_);
    glBufferSubData(GL_UNIFORM_BUFFER, 0, sizeof(FrameUniforms), &block);
    glBindBufferBase(GL_UNIFORM_BUFFER, kFrameBlockBinding, frame_ubo_);
  }

  // --- 3. DRAW LIST: one item per visible mesh, each with its sort key (render_list.h) ---
  void BuildDrawList(const Frame& frame, scene::RenderStats& stats) {
    const scene::Frustum frustum = scene::Frustum::FromMatrix(scene_view_projection_);
    for (const Frame::Drawable& drawable : frame.drawables) {
      scene::Node3D* node = drawable.node;
      const scene::AABB& box = drawable.box;
      if (!frustum.TestAABB(box)) {
        ++stats.culled_nodes;
        continue;
      }
      // Distance in front of the camera along its view direction (right for both camera kinds).
      const glm::vec3 center((box.min.x + box.max.x) * 0.5f, (box.min.y + box.max.y) * 0.5f,
                             (box.min.z + box.max.z) * 0.5f);
      const float depth = -(scene_view_ * glm::vec4(center, 1.0f)).z;
      const bool instanced = node->IsInstanced();
      const auto slot = static_cast<uint32_t>(node_slots_.size());
      // Normals: transpose(inverse(model)), once per node on the CPU (instanced copies do it per vertex).
      const glm::mat4 model = node->GetModelMatrix();
      node_slots_.push_back({node, model, instanced ? glm::mat3(1.0f) : glm::mat3(glm::transpose(glm::inverse(model))),
                             nullptr});  // instance data goes to the GPU after sorting (DrawViewport)

      for (const auto& mesh : node->GetModel()->meshes) {
        DrawItem item;
        item.node = node;
        item.mesh = mesh.get();
        item.node_slot = slot;
        item.material = EffectiveMaterial(*node, *mesh);
        item.blend = item.material ? item.material->blend : BlendMode::Opaque;
        item.depth_write = item.material ? item.material->DepthWrite() : true;
        item.custom = DrawableShaderMaterial(mesh->GetMaterial());
        if (!item.custom) item.custom = DrawableShaderMaterial(node->GetMaterial());
        // Compiled the first time (normally at warm-up); one that doesn't compile falls back to the
        // engine's shader.
        if (item.custom) item.custom_program = GLShaderFor(*item.custom->GetShader());
        if (!item.custom_program) item.custom = nullptr;
        GLuint program = 0;
        if (item.custom) {
          program = item.custom_program->Program();
        } else {
          item.program = &MeshProgramFor(MeshVariantKey(DrawFor(*node, *mesh, frame)));
          program = item.program->id;
          if (program == 0) continue;  // didn't compile (logged)
          if (const Material* textured = TexturedMaterial(*node, *mesh)) item.texture = GLTexture(*textured->GetTexture());
        }
        const auto index = static_cast<uint32_t>(draw_items_.size());
        draw_items_.push_back(item);
        const uint64_t key = item.blend == BlendMode::Opaque ? OpaqueSortKey(program, item.texture, depth)
                                                             : TransparentSortKey(depth, program);
        draw_order_.push_back({key, index});
      }
    }

    // Sprite batches: one item each (one instanced call for all its dots), the engine's sprite program.
    for (const Frame::Sprites& sprites : frame.sprites) {
      scene::SpriteBatch* batch = sprites.batch;
      if (!frustum.TestAABB(sprites.box)) {
        ++stats.culled_nodes;
        continue;
      }
      const scene::AABB& box = sprites.box;
      const glm::vec3 center((box.min.x + box.max.x) * 0.5f, (box.min.y + box.max.y) * 0.5f,
                             (box.min.z + box.max.z) * 0.5f);
      const float depth = -(scene_view_ * glm::vec4(center, 1.0f)).z;
      const WTexture* texture = SpriteTexture(*batch);
      const SpriteProgram& program = SpriteProgramFor(texture != nullptr);
      if (program.id == 0) continue;  // didn't compile (logged)

      DrawItem item;
      item.node = batch;
      item.sprites = batch;
      item.sprite_program = &program;
      item.node_slot = static_cast<uint32_t>(node_slots_.size());
      node_slots_.push_back({batch, batch->GetModelMatrix(), glm::mat3(1.0f), nullptr});
      item.material = batch->GetMaterial().get();
      item.blend = item.material ? item.material->blend : BlendMode::Additive;
      item.depth_write = item.material ? item.material->DepthWrite() : false;
      if (texture) item.texture = GLTexture(*texture);
      const auto index = static_cast<uint32_t>(draw_items_.size());
      draw_items_.push_back(item);
      const uint64_t key = item.blend == BlendMode::Opaque ? OpaqueSortKey(program.id, item.texture, depth)
                                                           : TransparentSortKey(depth, program.id);
      draw_order_.push_back({key, index});
    }
  }

  // --- 4. SHADOWS: every caster's depth, seen from the shadow light ---
  void DrawShadowMap(const Frame& frame) {
    GLint previous_framebuffer = 0;  // not always 0: the platform may draw into its own
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &previous_framebuffer);
    glBindFramebuffer(GL_FRAMEBUFFER, shadow_framebuffer_);
    glViewport(0, 0, shadow_map_size_, shadow_map_size_);
    state_.SetBlendMode(BlendMode::Opaque);
    state_.SetDepthWrite(true);
    glClear(GL_DEPTH_BUFFER_BIT);
    // A depth bias that grows with the surface's slope to the light: against "shadow acne".
    glEnable(GL_POLYGON_OFFSET_FILL);
    glPolygonOffset(1.5f, 3.0f);
    for (const Frame::Drawable& drawable : frame.drawables) {
      scene::Node3D* node = drawable.node;
      if (!node->CastsShadow()) continue;
      const ShadowProgram& program = ShadowProgramFor(node->IsInstanced());
      if (program.id == 0) continue;
      state_.UseProgram(program.id);
      glUniformMatrix4fv(program.model, 1, GL_FALSE, glm::value_ptr(node->GetModelMatrix()));
      GLInstanceBuffer* instances = PrepareInstances(*node);
      for (const auto& mesh : node->GetModel()->meshes) DrawMesh(*node, *mesh, instances, nullptr);
    }
    glDisable(GL_POLYGON_OFFSET_FILL);
    glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(previous_framebuffer));
  }

  // --- 5. MAIN PASS: the skybox, then the draw list in key order ---
  void DrawMainPass(const scene::Scene& scene, const Frame& frame, scene::RenderStats& stats) {
    if (const auto& skybox = scene.GetSkybox()) {
      glDisable(GL_DEPTH_TEST);
      glDisable(GL_CULL_FACE);
      state_.SetDepthWrite(false);
      state_.SetBlendMode(BlendMode::Opaque);
      state_.UseProgram(skybox_program_);
      glBindVertexArray(skybox_vao_);
      BindGLTexture(*skybox, 0);
      glDrawArrays(GL_TRIANGLES, 0, 36);
      glBindVertexArray(0);
      glEnable(GL_DEPTH_TEST);
    }

    // The shadow map on texture unit 1 (unit 0: the mesh's texture; units 2+: ShaderMaterials').
    if (frame.shadow_active) {
      glActiveTexture(GL_TEXTURE1);
      glBindTexture(GL_TEXTURE_2D, shadow_texture_);
      glActiveTexture(GL_TEXTURE0);
    }

    for (const DrawOrder& order : draw_order_) {
      const DrawItem& item = draw_items_[order.item];
      const NodeSlot& slot = node_slots_[item.node_slot];
      state_.SetBlendMode(item.blend);
      state_.SetDepthWrite(item.depth_write);
      if (IsTransparentKey(order.key)) ++stats.transparent_draw_calls;
      const glm::mat4& model = slot.model;
      if (item.sprites) {
        DrawSprites(item, model, stats);
        continue;
      }
      if (item.custom) {
        UseShaderMaterial(*item.custom, *item.custom_program, model, item.node->IsInstanced());
      } else {
        const MeshProgram& program = *item.program;
        state_.UseProgram(program.id);
        glUniformMatrix4fv(program.model, 1, GL_FALSE, glm::value_ptr(model));
        if (slot.instances == nullptr) {
          glUniformMatrix3fv(program.normal_matrix, 1, GL_FALSE, glm::value_ptr(slot.normal_matrix));
        }
        glUniform1f(program.shininess, item.material ? item.material->shininess : 32.0f);
        if (item.texture) state_.BindTexture0(item.texture);
      }
      DrawMesh(*item.node, *item.mesh, slot.instances, &stats);
    }

    state_.SetBlendMode(BlendMode::Opaque);  // back to the defaults for whatever draws next
    state_.SetDepthWrite(true);
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
  static GLInstanceBuffer* PrepareInstances(scene::Node3D& node, scene::RenderStats* stats = nullptr) {
    if (!node.IsInstanced()) return nullptr;
    if (!node.GetInstanceBuffer()) node.SetInstanceBuffer(std::make_unique<GLInstanceBuffer>());
    auto* buffer = static_cast<GLInstanceBuffer*>(node.GetInstanceBuffer());
    const size_t bytes = buffer->Upload(node.GetInstances(), node.GetInstancesVersion());
    if (stats) stats->instance_bytes += static_cast<int>(bytes);
    return buffer;
  }

  struct DrawItem;  // (below)
  // A sprite batch: its dots to the GPU if they changed (16 bytes each), then one instanced call.
  void DrawSprites(const DrawItem& item, const glm::mat4& model, scene::RenderStats& stats) {
    scene::SpriteBatch& batch = *item.sprites;
    if (!batch.GetSpriteBuffer()) batch.SetSpriteBuffer(std::make_unique<GLSpriteBuffer>(batch.Capacity()));
    auto* buffer = static_cast<GLSpriteBuffer*>(batch.GetSpriteBuffer());
    stats.instance_bytes += static_cast<int>(buffer->Upload(batch.Sprites(), batch.Version()));
    state_.UseProgram(item.sprite_program->id);
    glUniformMatrix4fv(item.sprite_program->model, 1, GL_FALSE, glm::value_ptr(model));
    if (item.texture) state_.BindTexture0(item.texture);
    const auto count = static_cast<GLsizei>(batch.Count());
    buffer->Draw(count);
    stats.instances += count;
    ++stats.draw_calls;
  }

  // The texture shaping a batch's dots (its material's), else null: the shader's own falloff.
  static const WTexture* SpriteTexture(const scene::SpriteBatch& batch) {
    const auto& material = batch.GetMaterial();
    return material && material->HasTexture() ? material->GetTexture() : nullptr;
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
  void UseShaderMaterial(const ShaderMaterial& material, const GLShader& shader, const glm::mat4& model,
                         bool instanced) {
    state_.UseProgram(shader.Program());
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
  using SpriteProgram = ShadowProgram;  // also just a program and its uModel

  // One mesh to draw this frame (main pass), made by BuildDrawList; drawn in the order of its key.
  struct DrawItem {
    scene::Node3D* node = nullptr;
    core::Mesh<core::Vertex3D>* mesh = nullptr;
    uint32_t node_slot = 0;                  // its node's per-frame data (node_slots_)
    const Material* material = nullptr;      // the material deciding its look (mesh's, else node's)
    const ShaderMaterial* custom = nullptr;  // drawn with this material's shader...
    const GLShader* custom_program = nullptr;  // (its program)
    const MeshProgram* program = nullptr;    // ...or with this engine variant
    scene::SpriteBatch* sprites = nullptr;   // a sprite batch instead of a mesh (mesh is null)...
    const SpriteProgram* sprite_program = nullptr;  // ...with the sprite program
    GLuint texture = 0;                      // engine variant / sprites: its texture on unit 0 (0 = none)
    BlendMode blend = BlendMode::Opaque;
    bool depth_write = true;
  };
  // What a node's meshes share, worked out once per frame.
  struct NodeSlot {
    scene::Node3D* node = nullptr;
    glm::mat4 model{1.0f};                   // world matrix (walks the parents once)
    glm::mat3 normal_matrix{1.0f};           // plain nodes (instanced: per vertex)
    GLInstanceBuffer* instances = nullptr;   // instanced nodes (set just before drawing)
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
    state_.UseProgram(program.id);
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

  // The sprite batches' program: plain (the dot computed) or TEXTURED (the dot from its texture).
  const SpriteProgram& SpriteProgramFor(bool textured) {
    SpriteProgram& program = sprite_programs_[textured ? 1 : 0];
    if (program.id != 0 || sprite_failed_[textured ? 1 : 0]) return program;
    const auto start = std::chrono::steady_clock::now();
    program.id = BuildEngineProgram("sprite.vert", "sprite.frag",
                                    textured ? std::vector<std::string>{"TEXTURED"} : std::vector<std::string>{});
    if (program.id == 0) {
      sprite_failed_[textured ? 1 : 0] = true;  // logged once, not retried every frame
      return program;
    }
    program.model = glGetUniformLocation(program.id, "uModel");
    if (textured) {
      state_.UseProgram(program.id);
      glUniform1i(glGetUniformLocation(program.id, "uTexture"), 0);
    }
    if (warmed_up_) {
      const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
      WLW_LOG_INFO("Sprite shader compiled during play (%.1f ms)%s", ms, textured ? ": TEXTURED" : "");
    }
    return program;
  }

  static std::string VariantName(uint32_t key) {
    std::string name;
    for (const std::string& define : MeshVariantDefines(key)) name += " " + define;
    return name.empty() ? " (none)" : name;
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
      for (const SpriteProgram& program : sprite_programs_) {
        if (program.id) glDeleteProgram(program.id);
      }
      if (skybox_program_) glDeleteProgram(skybox_program_);
      if (frame_ubo_) glDeleteBuffers(1, &frame_ubo_);
      DeleteShadowMap();
      glDeleteVertexArrays(1, &skybox_vao_);
      glDeleteBuffers(1, &skybox_vbo_);
      glDeleteVertexArrays(1, &post_vao_);
      copy_shader_.reset();
      gpu_timer_.reset();
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
  SpriteProgram sprite_programs_[2];                          // computed dot, textured dot
  bool sprite_failed_[2] = {false, false};
  GLuint skybox_program_ = 0;
  GLuint skybox_vao_ = 0;
  GLuint skybox_vbo_ = 0;
  GLuint post_vao_ = 0;                  // empty: post passes draw a triangle made in the shader
  std::shared_ptr<Shader> copy_shader_;  // a post pass without a usable shader
  std::unique_ptr<GpuTimer> gpu_timer_;  // null: this GPU can't time
  bool timing_ = false;                  // timing this frame
  std::vector<GpuSample> gpu_samples_;   // results arriving this frame
  std::vector<float> raw_pass_ms_;       // scratch for ApplyGpuSamples
  GLuint frame_ubo_ = 0;
  GLStateCache state_;          // program, blend, depth writes, unit-0 texture: skips redundant GL calls
  // Per-viewport lists, reused every frame (cleared, never shrunk: no allocations once warm).
  Frame frame_;
  std::vector<DrawItem> draw_items_;
  std::vector<NodeSlot> node_slots_;
  std::vector<DrawOrder> draw_order_;
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
