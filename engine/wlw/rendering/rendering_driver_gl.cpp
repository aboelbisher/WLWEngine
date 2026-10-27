#ifdef WLW_RENDERER_GL

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <unordered_map>
#include <vector>

#include <glm/gtc/type_ptr.hpp>

#include "wlw/rendering/gl.h"

#include "wlw/core/assert.h"
#include "wlw/core/profile.h"
#include "wlw/platform/platform.h"
#include "rendering_driver.h"
#include "wlw/rendering/frame_uniforms.h"
#include "wlw/rendering/gl_blend.h"
#include "wlw/rendering/gl_gpu_profile.h"
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
#include "wlw/rendering/scene_gather.h"
#include "wlw/rendering/sky.h"
#include "wlw/rendering/gl_state_cache.h"
#include "wlw/scene/geometry_instance.h"
#include "wlw/scene/light.h"
#include "wlw/scene/shadow_map.h"
#include "wlw/scene/billboard_batch.h"
#include "wlw/scene/sprite_batch.h"
#include "wlw/scene/window.h"
#include "wlw/ui/ui_renderer.h"

namespace wlw::rendering {

// The 3D renderer on GL 3.3 / GLES 3.0. Per viewport:
//   1. gather   visible lights and drawables (each one's world box computed once), shadow bounds
//   2. frame    the per-frame uniform block (camera, lights, shadow) uploaded once (frame_uniforms.h)
//   3. shadows  casters' depth from the shadow light into the shadow map
//   4. main     opaque meshes, the sky (sky.h), then transparent ones back to front; each mesh with the
//               shader variant its features need (mesh_variant.h), or its ShaderMaterial's shader
//   5. 2D        its Picture canvas layers (part of the picture)
//   6. post      its post passes, if any (steps 3-5 then drew into the scene picture): full-screen
//                shaders, the last one writing the viewport's picture (post_pass.h)
//   7. 2D        its Overlay canvas layers on top (its default canvas = the game UI on the screen)
class GLRenderingDriver : public RenderingDriver {
 public:
  explicit GLRenderingDriver(RenderDevice* device) : device_(device) {}

  bool Initialize(std::shared_ptr<scene::Window> window) override {
    main_window_ = window;

    // The platform layer creates the OS window and its GL context (GLFW on desktop, EGL on
    // Android); the renderer only needs a current context and loaded GL functions.
    if (!platform::Init()) {
      WLW_LOG(LogRender, Error, "Platform initialization failed");
      return false;
    }
    platform_started_ = true;
    if (!window->Initialize()) {
      WLW_LOG(LogRender, Error, "Window / GL context creation failed");
      return false;
    }
    window->MakeContextCurrent();
    if (!platform::LoadGLFunctions()) {
      WLW_LOG(LogRender, Error, "Failed to load OpenGL functions");
      return false;
    }

    gpu_name_ = reinterpret_cast<const char*>(glGetString(GL_RENDERER));
    WLW_LOG(LogRender, Info, "--- OpenGL renderer initialized ---\n  Vendor:   %s\n  Renderer: %s\n  Version:  %s",
            reinterpret_cast<const char*>(glGetString(GL_VENDOR)),
            reinterpret_cast<const char*>(glGetString(GL_RENDERER)),
            reinterpret_cast<const char*>(glGetString(GL_VERSION)));
    window->Internal().SetFragmentPrecision(DescribeFragmentPrecision());
    WLW_LOG(LogRender, Info, "  Fragment mediump: %s", window->FragmentPrecision().c_str());

    skybox_program_ = BuildEngineProgram("skybox.vert", "skybox.frag");
    if (skybox_program_ == 0) return false;
    glUseProgram(skybox_program_);
    glUniform1i(glGetUniformLocation(skybox_program_, "uSkybox"), 0);
    skybox_ray_ = glGetUniformLocation(skybox_program_, "uSkyRay");
    glUseProgram(0);

    // GL 3.3 core draws need a vertex array, even an empty one (post passes and the sky make
    // their triangle in the shader).
    glGenVertexArrays(1, &post_vao_);
    CopyShader();  // made now, so the warm-up compiles it
    // The largest picture it can draw into (a colour texture + a depth renderbuffer): render
    // scales above 1 stop there.
    GLint max_texture = 0, max_renderbuffer = 0;
    glGetIntegerv(GL_MAX_TEXTURE_SIZE, &max_texture);
    glGetIntegerv(GL_MAX_RENDERBUFFER_SIZE, &max_renderbuffer);
    max_target_size_ = std::min(max_texture, max_renderbuffer);

    // The per-frame uniform block's buffer: every program reads it at kFrameBlockBinding.
    glGenBuffers(1, &frame_ubo_);
    glBindBuffer(GL_UNIFORM_BUFFER, frame_ubo_);
    glBufferData(GL_UNIFORM_BUFFER, sizeof(FrameUniforms), nullptr, GL_DYNAMIC_DRAW);
    glBindBufferBase(GL_UNIFORM_BUFFER, kFrameBlockBinding, frame_ubo_);

    // Draws each viewport's 2D canvas (the screen's is the game UI).
    ui_renderer_ = ui::UIRenderer::Create();

    // GPU timing, if this GPU can (gpu_timer.h).
    if (auto queries = CreateGLGpuQueries()) gpu_timer_ = std::make_unique<GpuTimer>(std::move(queries));
    window->Internal().SetGpuTimingSupported(gpu_timer_ != nullptr);
    StartGpuProfiler();  // the profiler's GPU zones (gl_gpu_profile.h), profile builds only

    glEnable(GL_DEPTH_TEST);
    return true;
  }

  std::string GpuName() const override { return gpu_name_; }

  std::vector<uint8_t> ReadPixels(RenderTarget& target) override {
    if (target.Internal().Gpu() == nullptr) return {};  // never drawn into
    const int width = target.Width(), height = target.Height();
    const size_t texels = static_cast<size_t>(width) * height;
    std::vector<uint8_t> pixels(texels * 4);
    GLint previous = 0;
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &previous);
    glBindFramebuffer(GL_FRAMEBUFFER, GLFramebuffer(target));
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    const TextureFormat format = target.Settings().format;
    if (format == TextureFormat::R16F || format == TextureFormat::RGBA16F) {
      // Float targets read back as floats (the pair GLES guarantees for them).
      std::vector<float> values(texels * 4);
      glReadPixels(0, 0, width, height, GL_RGBA, GL_FLOAT, values.data());
      for (size_t i = 0; i < values.size(); ++i) {
        pixels[i] = static_cast<uint8_t>(std::lround(std::clamp(values[i], 0.0f, 1.0f) * 255.0f));
      }
    } else {
      glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
    }
    glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(previous));
    // GL's row 0 is the bottom of the picture; images start at the top.
    const size_t row = static_cast<size_t>(width) * 4;
    for (int y = 0; y < height / 2; ++y) {
      std::swap_ranges(pixels.begin() + y * row, pixels.begin() + (y + 1) * row,
                       pixels.begin() + (height - 1 - y) * row);
    }
    return pixels;
  }

  // What one viewport draws, gathered from its scene before any drawing starts.
  // The scene part (lights, drawables, sprite + billboard batches, shadow boxes) is GL-free: scene_gather.h.
  struct Frame : SceneGather {
    const scene::Light3D* shadow_light = nullptr;  // the directional light that casts shadows, if any
    bool shadow_active = false;                    // a shadow map is drawn this frame
    scene::ShadowFrustum shadow_frustum;

    // Empties it for the next viewport, keeping the lists' memory (no allocations per frame).
    void Clear() {
      SceneGather::Clear();
      shadow_light = nullptr;
      shadow_active = false;
    }
  };

  // One frame of a window: its offscreen viewports into their textures (in the order added, so
  // one may use the picture of an earlier one), then the screen, the overlays, and present.
  void DrawWindow(std::shared_ptr<scene::Window> window) override {
    WLW_PROFILE_SCOPE("Draw window");
    window->MakeContextCurrent();

    GLint screen_framebuffer = 0;  // not always 0: the platform may draw into its own
    {
      // A glGet: drivers that queue GL calls on their own thread (desktop NVIDIA) wait for it here.
      WLW_PROFILE_SCOPE("Screen framebuffer");
      glGetIntegerv(GL_FRAMEBUFFER_BINDING, &screen_framebuffer);
    }
    const glm::vec2 screen_pixels{window->GetSize().x, window->GetSize().y};

    // GPU timing: this frame's sections are measured; the results of one a few frames ago arrive.
    timing_ = gpu_timer_ && window->GpuTiming();
    invalidate_ = window->InvalidateFramebuffers();
    if (timing_) {
      WLW_PROFILE_SCOPE("GPU timer read");
      gpu_timer_->BeginFrame(gpu_samples_);
      ApplyGpuSamples(*window);
    }

    for (const auto& viewport : window->Viewports()) {
      RenderTarget* target = viewport->Internal().GetTarget();
      if (target == nullptr) continue;  // only the window's own MainViewport() draws to the screen
      const glm::ivec2 pixels = viewport->PixelSize(screen_pixels);
      if (!viewport->Internal().BeginDraw(pixels)) continue;
      target->SetSize(pixels.x, pixels.y);
      DrawViewport(*viewport, pixels, GLFramebuffer(*target), target->Settings().depth);
    }

    DrawViewport(window->MainViewport(), {static_cast<int>(screen_pixels.x), static_cast<int>(screen_pixels.y)},
                 static_cast<GLuint>(screen_framebuffer), window->ScreenHasDepth());
    if (timing_) gpu_timer_->End();
    {
      WLW_PROFILE_SCOPE("Overlays");
      WLW_PROFILE_GPU_ZONE("Overlays");
      window->Internal().DrawOverlays();  // e.g. the debug UI: on top of everything (not timed)
    }
    // The screen's depth (and stencil) end with the frame: not written back to memory.
    glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(screen_framebuffer));
    if (window->ScreenHasDepth()) Invalidate(static_cast<GLuint>(screen_framebuffer), /*color=*/false, /*depth=*/true);

    // Present the frame we just drew (previously the swap happened before drawing,
    // so every frame was shown one frame late).
    {
      WLW_PROFILE_SCOPE("Present");
      window->SwapBuffers();
    }
    CollectGpuProfiler();  // finished GPU zones -> the profiler
  }

  void WarmUp(scene::Window& window) override {
    WLW_PROFILE_SCOPE("Shader warm-up");
    window.MakeContextCurrent();
    const auto start = std::chrono::steady_clock::now();
    const size_t before = mesh_programs_.size();
    // Every node with a mesh counts, visible or not and with or without copies: games build
    // pools of hidden nodes and instanced nodes that fill up later, and those draw soon.
    auto warm = [this](const scene::Viewport& viewport) {
      if (!viewport.GetScene()) return;
      Frame frame;
      bool any_caster = false;
      viewport.GetScene()->ForEachNode([&](scene::Node3D& node) {
        switch (node.Kind()) {
          case scene::NodeKind::SpriteBatch:
            QuadProgramFor(QuadShader::Sprite,
                           MaterialTexture(static_cast<const scene::SpriteBatch&>(node).GetMaterial()) != nullptr);
            break;
          case scene::NodeKind::BillboardBatch:
            QuadProgramFor(QuadShader::Billboard,
                           MaterialTexture(static_cast<const scene::BillboardBatch&>(node).GetMaterial()) != nullptr);
            break;
          case scene::NodeKind::Light: {
            const auto* light = static_cast<const scene::Light3D*>(&node);
            frame.lights.push_back(light);
            const bool shadows = light->CastsShadows() && light->GetType() == scene::Light3D::Type::Directional;
            if (shadows && frame.shadow_light == nullptr) frame.shadow_light = light;
            break;
          }
          case scene::NodeKind::MeshInstance:
          case scene::NodeKind::MultiMeshInstance: {
            auto& geometry = static_cast<scene::GeometryInstance3D&>(node);
            if (geometry.GetModel() && !geometry.GetModel()->meshes.empty()) {
              frame.drawables.push_back({&geometry, {}});
              any_caster |= geometry.CastsShadow();
            }
            break;
          }
          case scene::NodeKind::Node:
          case scene::NodeKind::Camera: break;
        }
      });
      frame.shadow_active = frame.shadow_light != nullptr && any_caster;  // as if the map can be made
      for (const Frame::Drawable& drawable : frame.drawables) {
        scene::GeometryInstance3D* node = drawable.node;
        for (const auto& mesh : node->GetModel()->meshes) {
          if (!DrawableShaderMaterial(mesh->GetMaterial()) && !DrawableShaderMaterial(node->GetMaterial())) {
            MeshProgramFor(MeshVariantKey(DrawFor(*node, *mesh, frame)));
          }
        }
        if (frame.shadow_active && node->CastsShadow()) ShadowProgramFor(IsMultiMesh(*node));
      }
    };
    for (const auto& viewport : window.Viewports()) warm(*viewport);
    warm(window.MainViewport());
    OutputMaterial();   // made now so the warm-up compiles it (any viewport may turn tone mapping on later)
    MakeBloomPasses();  // the same for bloom
    // Every game shader and texture alive now (canvas shaders too: no scene says they're needed).
    const int custom = PrepareAliveResources();
    warmed_up_ = true;
    const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
    WLW_LOG(LogRender, Info, "Shader warm-up: %zu mesh variant(s) + %d game shader(s) compiled in %.1f ms",
            mesh_programs_.size() - before, custom, ms);
  }

  // One viewport into `destination` (the screen's framebuffer or its target's): its scene through
  // its camera, its post passes if it has any (the scene then goes into the scene picture first,
  // and the last pass writes `destination`), then its canvas over it. A render scale other than 1
  // draws the 3D and Picture layers into the scene picture at the render size too; the passes, or
  // a copy without them, stretch it to `destination`.
  void DrawViewport(scene::Viewport& viewport, glm::ivec2 pixels, GLuint destination, bool has_depth) {
    WLW_PROFILE_SCOPE("Viewport");
#if WLW_PROFILE
    char label[32];
    const int label_size = viewport.IsScreen()
                               ? std::snprintf(label, sizeof(label), "screen")
                               : std::snprintf(label, sizeof(label), "offscreen %u", viewport.Internal().Id());
    WLW_PROFILE_TEXT(label, static_cast<size_t>(label_size));
#endif
    using Clock = std::chrono::steady_clock;
    const glm::ivec2 render = FitRenderSize(viewport.RenderSize(pixels));  // == pixels at render scale 1
    // With post passes, the output pass or a render scale the 3D goes into the scene picture, a
    // target of the viewport's own format, at the render size; so does 3D that needs depth when the
    // destination has none (a screen made without a depth buffer: WindowOptions::depth), and the 3D
    // of an HDR screen (the window's surface is 8-bit).
    const TextureFormat format = viewport.EffectiveFormat();
    const bool needs_picture = viewport.Post().HasWork() || render != pixels ||
                               (viewport.Settings().depth && !has_depth) ||
                               (viewport.IsScreen() && format != TextureFormat::RGBA8);
    const PostPlan* post = nullptr;
    GLuint scene_framebuffer = destination;
    // What the 3D is drawn into, after any fallback (the destination's format without a picture).
    const RenderTarget* own_target = viewport.Internal().GetTarget();
    TextureFormat drawn_format = own_target ? own_target->Settings().format : TextureFormat::RGBA8;
    if (needs_picture && pixels.x > 0 && pixels.y > 0) {
      const scene::ViewportSettings& settings = viewport.Settings();
      post = &viewport.Post().Internal().Prepare(pixels, {format, settings.smooth, settings.depth}, render);
      scene_framebuffer = GLFramebuffer(viewport.Post().Internal().SceneTarget());
      drawn_format = viewport.Post().Internal().SceneTarget().Settings().format;
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
      WLW_PROFILE_SCOPE("Gather");
      // The viewport's shape, not the render size: a scaled picture is stretched back to it. A minimized window
      // has no size.
      const float aspect = pixels.y > 0 ? static_cast<float>(pixels.x) / static_cast<float>(pixels.y) : 1.0f;
      GatherScene(*scene, frame_);
      PrepareShadow(frame_);
      SetCameraMatrices(*camera, aspect);
      BuildDrawList(frame_, stats);
    }
    const auto gathered = Clock::now();
    {
      WLW_PROFILE_SCOPE("Sort");
      SortDrawOrder(draw_order_);
    }
    const auto sorted = Clock::now();
    // GPU work from here on: the frame block, instance data, the shadow and main passes.
    glBindFramebuffer(GL_FRAMEBUFFER, scene_framebuffer);  // (the shadow pass returns to it)
    TimeSection(viewport.Internal().Id(), GpuSection::Kind::Scene);
    if (draw_scene) {
      {
        WLW_PROFILE_SCOPE("Upload");
        UploadFrame(*camera, frame_);
        for (NodeSlot& slot : node_slots_) {
          if (slot.node) slot.instances = PrepareInstances(*slot.node, &stats);
        }
      }
      if (frame_.shadow_active) DrawShadowMap(frame_);
    }

    glViewport(0, 0, render.x, render.y);
    state_.SetDepthWrite(true);
    GLbitfield clear_bits = has_depth ? GL_DEPTH_BUFFER_BIT : 0;
    if (const auto& color = viewport.GetClearColor()) {
      glClearColor(color->r, color->g, color->b, color->a);
      clear_bits |= GL_COLOR_BUFFER_BIT;
    }
    if (clear_bits) glClear(clear_bits);

    if (draw_scene) DrawMainPass(*scene, frame_, has_depth, stats);
    const auto submitted = Clock::now();

    auto micros = [](Clock::duration d) { return std::chrono::duration<float, std::micro>(d).count(); };
    stats.gather_us = micros(gathered - start);
    stats.sort_us = micros(sorted - gathered);
    stats.submit_us = micros(submitted - sorted);
    stats.program_changes = state_.ProgramChanges();
    stats.texture_binds = state_.TextureBinds();
    stats.state_calls_skipped = state_.Skipped();
    stats.render_width = render.x;
    stats.render_height = render.y;
    stats.picture_format = drawn_format;

    // 2D: Picture layers into the picture (before the passes, at its render size), Overlay layers
    // on top after them (full size: the UI stays sharp at any render scale).
    auto draw_layers = [&](scene::CanvasMode mode, glm::ivec2 size) {
      bool timed = false;
      const glm::vec2 layer_pixels{static_cast<float>(size.x), static_cast<float>(size.y)};
      viewport.Internal().ForEachCanvasLayer(mode, [&](const ui::Canvas& canvas) {
        if (canvas.QuadCount() == 0) return;
        if (!timed) TimeSection(viewport.Internal().Id(), GpuSection::Kind::Canvas);
        timed = true;
        WLW_PROFILE_SCOPE("Canvas");
        WLW_PROFILE_GPU_ZONE("Canvas");
        ui_renderer_->Draw(canvas, layer_pixels);
      });
      if (timed && timing_) gpu_timer_->End();
    };
    draw_layers(scene::CanvasMode::Picture, render);
    state_.Reset();  // the UI renderer changed program, blend and textures behind the cache's back

    if (post) {
      // Nothing reads the scene picture's depth after its 3D: not written back to memory.
      if (has_depth) stats.invalidations += Invalidate(scene_framebuffer, /*color=*/false, /*depth=*/true);
      const auto post_start = Clock::now();
      stats.invalidations += DrawPostPasses(viewport.Post(), *post, destination, viewport.Internal().Id());
      stats.post_passes = static_cast<int>(post->steps.size());
      stats.post_targets = viewport.Post().Internal().TargetCount();
      stats.bloom_levels = post->bloom_levels;
      stats.post_us = micros(Clock::now() - post_start);
    }
    draw_layers(scene::CanvasMode::Overlay, pixels);
    // An offscreen viewport's depth isn't needed after it's drawn (the screen's: after the frame).
    if (!post && has_depth && !viewport.IsScreen()) {
      glBindFramebuffer(GL_FRAMEBUFFER, destination);
      stats.invalidations += Invalidate(destination, /*color=*/false, /*depth=*/true);
    }
    viewport.Internal().SetRenderStats(stats);
  }

  // A render size the GPU can draw into: past its largest texture (a big render scale on a big
  // screen), scaled down to fit, keeping its shape.
  glm::ivec2 FitRenderSize(glm::ivec2 size) {
    const int largest = std::max(size.x, size.y);
    if (max_target_size_ <= 0 || largest <= max_target_size_) return size;
    if (!warned_render_size_) {
      WLW_LOG(LogRender, Warning, "Render size %d x %d is more than this GPU draws into (%d): made smaller", size.x,
              size.y, max_target_size_);
      warned_render_size_ = true;
    }
    const double fit = static_cast<double>(max_target_size_) / largest;
    return {std::clamp(static_cast<int>(size.x * fit), 1, max_target_size_),
            std::clamp(static_cast<int>(size.y * fit), 1, max_target_size_)};
  }

  // --- FRAMEBUFFER INVALIDATION: what a tiled GPU needn't load or store ---
  // Phone GPUs (Mali, Adreno, PowerVR, Apple) draw a framebuffer in small tiles in on-chip memory:
  // a tile's old contents are loaded from memory before drawing and stored back after, unless told
  // they aren't needed (glInvalidateFramebuffer; Vulkan / Metal: loadOp / storeOp DONT_CARE). For
  // the BOUND framebuffer ramebuffer (0 = the window's own). Returns 1 if it called GL. Desktop
  // GL 3.3 has no such call and desktop GPUs aren't tiled: nothing there.
  int Invalidate(GLuint framebuffer, bool color, bool depth) {
    if (!invalidate_) return 0;
#if defined(WLW_GLES)
    GLenum attachments[3];
    GLsizei count = 0;
    const bool window = framebuffer == 0;  // the window's attachments have their own names
    if (color) attachments[count++] = window ? GL_COLOR : GL_COLOR_ATTACHMENT0;
    if (depth) {
      attachments[count++] = window ? GL_DEPTH : GL_DEPTH_ATTACHMENT;
      attachments[count++] = window ? GL_STENCIL : GL_STENCIL_ATTACHMENT;
    }
    if (count == 0) return 0;
    glInvalidateFramebuffer(GL_FRAMEBUFFER, count, attachments);
    return 1;
#else
    (void)framebuffer, (void)color, (void)depth;
    return 0;
#endif
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
    window.Internal().SetGpuFrameMs(SmoothMs(window.GpuFrameMs(), frame_ms));
    auto apply = [this](scene::Viewport& viewport) {
      float scene_ms = -1.0f, post_ms = -1.0f, canvas_ms = -1.0f;
      raw_pass_ms_.clear();
      for (const GpuSample& sample : gpu_samples_) {
        if (sample.section.viewport != viewport.Internal().Id()) continue;
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
      scene::GpuStats& stats = viewport.Internal().MutableGpuStats();
      stats.scene_ms = scene_ms < 0.0f ? -1.0f : SmoothMs(stats.scene_ms, scene_ms);
      stats.canvas_ms = canvas_ms < 0.0f ? -1.0f : SmoothMs(stats.canvas_ms, canvas_ms);
      stats.post_ms = post_ms < 0.0f ? -1.0f : SmoothMs(stats.post_ms, post_ms);
      stats.pass_ms.resize(raw_pass_ms_.size(), -1.0f);
      for (size_t i = 0; i < raw_pass_ms_.size(); ++i) stats.pass_ms[i] = SmoothMs(stats.pass_ms[i], raw_pass_ms_[i]);
    };
    for (const auto& viewport : window.Viewports()) apply(*viewport);
    apply(window.MainViewport());
  }

  // --- POST PASSES: full-screen shaders from the scene picture to `destination` (post_pass.h) ---
  // Returns the framebuffer invalidations it made.
  int DrawPostPasses(PostChain& chain, const PostPlan& plan, GLuint destination, uint32_t viewport_id) {
    int invalidations = 0;
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    state_.SetBlendMode(BlendMode::Opaque);  // each pass replaces its output's pixels
    glBindVertexArray(post_vao_);            // no attributes: post.vert makes the triangle from gl_VertexID
    const std::vector<PostPass>& passes = chain.Passes();
    const PostSettings settings = chain.EffectiveSettings();
    for (size_t index = 0; index < plan.steps.size(); ++index) {
      const PostStep& step = plan.steps[index];
      // A scaled viewport without passes: the step that stretches its picture (no material: a copy).
      const bool bloom_step = step.pass == kBloomDownPass || step.pass == kBloomUpPass;
      const PostPass& pass = step.pass == kUpscalePass  ? upscale_pass_
                             : step.pass == kOutputPass ? OutputPass(settings, plan.bloom_levels > 0)
                             : bloom_step               ? BloomPass(step, settings.bloom)
                                                        : passes[static_cast<size_t>(step.pass)];
      WLW_PROFILE_SCOPE_DYNAMIC(pass.name.empty() ? "Post pass" : pass.name.c_str());
      WLW_PROFILE_GPU_ZONE_DYNAMIC(pass.name.empty() ? "Post pass" : pass.name.c_str());
      TimeSection(viewport_id, GpuSection::Kind::PostPass, static_cast<int>(index));
      const GLuint output =
          step.output == kDestination ? destination : GLFramebuffer(chain.Internal().Target(step.output));
      glBindFramebuffer(GL_FRAMEBUFFER, output);
      glViewport(0, 0, step.size.x, step.size.y);
      // The pass overwrites every pixel: the old picture needn't be loaded first.
      invalidations += Invalidate(output, /*color=*/true, /*depth=*/false);

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

      const WTexture2D& source = *chain.Internal().TextureOf(step.source);
      BindGLTexture(source, 0);
      glUniform1i(shader->EngineInputs().source, 0);
      BindGLTexture(*chain.Internal().TextureOf(kSceneTarget), 1);
      glUniform1i(shader->EngineInputs().scene, 1);
      glUniform2f(shader->EngineInputs().texel, 1.0f / static_cast<float>(source.Width()),
                  1.0f / static_cast<float>(source.Height()));
      glUniform2f(shader->EngineInputs().output_size, static_cast<float>(step.size.x), static_cast<float>(step.size.y));
      int unit = 2;
      for (const auto& [uniform, texture] : step.inputs) {
        BindGLTexture(*chain.Internal().TextureOf(texture), static_cast<unsigned int>(unit));
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
    return invalidations;
  }

  // The output pass (post_settings.h): one engine shader, shared by every viewport; its params are
  // set from the viewport's settings just before it draws.
  const std::shared_ptr<ShaderMaterial>& OutputMaterial() {
    if (!output_pass_.material) {
      const std::string* text = FindShaderFile("post/output.glsl");
      output_pass_.material = ShaderMaterial::Create(Shader::Create(text ? *text : std::string(), "post/output.glsl"));
    }
    return output_pass_.material;
  }
  const PostPass& OutputPass(const PostSettings& settings, bool bloom) {
    ShaderMaterial& material = *OutputMaterial();
    const ToneMapSettings& tone_map = settings.tone_map;
    material.SetParam("uExposure", tone_map.exposure);
    material.SetParam("uShoulder", tone_map.mapper == ToneMapper::Shoulder ? 1.0f : 0.0f);
    material.SetParam("uDither", tone_map.dither ? 1.0f : 0.0f);
    material.SetParam("uBloomIntensity", bloom ? std::max(settings.bloom.intensity, 0.0f) : 0.0f);
    return output_pass_;
  }

  // The bloom's steps (post_settings.h): two engine shaders, one material each, shared by every
  // level and viewport (a pass per level only for its profiler name); params set just before each draw.
  void MakeBloomPasses() {
    if (bloom_down_[0].material) return;
    auto make = [](const char* file) {
      const std::string* text = FindShaderFile(file);
      return ShaderMaterial::Create(Shader::Create(text ? *text : std::string(), file));
    };
    const std::shared_ptr<ShaderMaterial> down = make("post/bloom_down.glsl");
    const std::shared_ptr<ShaderMaterial> up = make("post/bloom_up.glsl");
    for (int k = 0; k < kMaxBloomLevels; ++k) {
      bloom_down_[k] = {.material = down, .name = "Bloom down " + std::to_string(k)};
      bloom_up_[k] = {.material = up, .name = "Bloom up " + std::to_string(k)};
    }
  }
  const PostPass& BloomPass(const PostStep& step, const BloomSettings& bloom) {
    MakeBloomPasses();
    const int level = std::clamp(step.level, 0, kMaxBloomLevels - 1);
    if (step.pass == kBloomUpPass) {
      bloom_up_[level].material->SetParam("uScatter", std::clamp(bloom.scatter, 0.0f, 1.0f));
      return bloom_up_[level];
    }
    ShaderMaterial& material = *bloom_down_[level].material;
    material.SetParam("uFirst", level == 0 ? 1.0f : 0.0f);
    material.SetParam("uThreshold", bloom.threshold);
    material.SetParam("uKnee", std::max(bloom.knee, 0.0f));
    return bloom_down_[level];
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
    rendering::GatherScene(scene, frame);  // the scene walk (scene_gather.h)

    // The shadow light: the first directional light that casts shadows.
    for (const scene::Light3D* light : frame.lights) {
      if (!light->CastsShadows()) continue;
      if (light->GetType() != scene::Light3D::Type::Directional) {
        if (!warned_shadow_type_)
          WLW_LOG(LogRender, Warning, "Only directional lights cast shadows (for now): ignoring the others");
        warned_shadow_type_ = true;
        continue;
      }
      if (frame.shadow_light == nullptr) frame.shadow_light = light;
    }
  }

  // The shadow light's box around the casters, and the shadow map to draw it into.
  void PrepareShadow(Frame& frame) {
    if (frame.shadow_light == nullptr || !frame.has_casters) return;
    const scene::Light3D& light = *frame.shadow_light;
    if (!EnsureShadowMap(light.shadow.map_size)) return;
    frame.shadow_frustum = scene::FitDirectionalShadow(
        light.GetDirection(), frame.casters, frame.has_receivers ? frame.receivers : frame.casters, shadow_map_size_);
    frame.shadow_active = true;
  }

  // This viewport's camera at its aspect, for culling and depth (BuildDrawList), the frame block and the sky: no GL.
  void SetCameraMatrices(const scene::Camera3D& camera, float aspect) {
    scene_view_ = camera.GetViewMatrix();
    scene_projection_ = camera.GetProjectionMatrix(aspect);
    scene_view_projection_ = scene_projection_ * scene_view_;
    scene_cull_mask_ = camera.GetCullMask();
  }

  // --- 2. FRAME: the per-frame uniform block, read by every program this viewport draws with ---
  void UploadFrame(const scene::Camera3D& camera, const Frame& frame) {
    FrameUniforms block;
    block.view = scene_view_;
    block.projection = scene_projection_;
    block.view_projection = scene_view_projection_;
    const Vec3 position = camera.GetWorldPosition();
    // The projection tells the two kinds apart: w is 1 for orthographic (no divide by depth), -z for
    // perspective. Row 2 of the view matrix is the camera's backward axis in world space.
    block.camera_position = {position, block.projection[3][3] == 1.0f ? 1.0f : 0.0f};
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
    // Culling, depths and matrices: GL-free (scene_gather.h).
    CullScene(frame, scene_view_, scene_view_projection_, visible_, scene_cull_mask_);
    stats.culled_nodes += visible_.culled;
    for (const VisibleSet::Node& visible : visible_.nodes) {
      scene::GeometryInstance3D* node = visible.node;
      const float depth = visible.depth;
      const auto slot = static_cast<uint32_t>(node_slots_.size());
      // Instance data goes to the GPU after sorting (DrawViewport).
      node_slots_.push_back({node, visible.model, visible.normal_matrix, nullptr});

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
          if (const Material* textured = TexturedMaterial(*node, *mesh))
            item.texture = GLTexture(*textured->GetTexture());
        }
        const auto index = static_cast<uint32_t>(draw_items_.size());
        draw_items_.push_back(item);
        // A game shader's draws group by material within the program (its params are uploaded once per group).
        const auto group = item.custom ? static_cast<uint32_t>(item.custom->Internal().Id()) : item.texture;
        const uint64_t key =
            item.blend == BlendMode::Opaque ? OpaqueSortKey(program, group, depth) : TransparentSortKey(depth, program);
        draw_order_.push_back({key, index});
      }
    }

    // Sprite and billboard batches: one item each (one instanced call for all its quads), the engine's quad programs.
    for (const VisibleSet::Sprites& sprites : visible_.sprites)
      AddQuadItem(QuadShader::Sprite, sprites.batch, sprites.batch->GetMaterial(), sprites.model, sprites.depth);
    for (const VisibleSet::Billboards& billboards : visible_.billboards) {
      AddQuadItem(QuadShader::Billboard, billboards.batch, billboards.batch->GetMaterial(), billboards.model,
                  billboards.depth);
    }
  }

  // The engine programs that draw instanced quads (one per batch type).
  enum class QuadShader : uint8_t { Sprite, Billboard };

  // A sprite or billboard batch's draw item: its program (plain or TEXTURED), blend and sort key.
  void AddQuadItem(QuadShader shader, scene::Node3D* batch, const std::shared_ptr<Material>& material,
                   const glm::mat4& model, float depth) {
    const WTexture* texture = MaterialTexture(material);
    const QuadProgram& program = QuadProgramFor(shader, texture != nullptr);
    if (program.id == 0) return;  // didn't compile (logged)

    DrawItem item;
    item.quads = batch;
    item.quad_shader = shader;
    item.quad_program = &program;
    item.node_slot = static_cast<uint32_t>(node_slots_.size());
    node_slots_.push_back({nullptr, model, glm::mat3(1.0f), nullptr});
    item.material = material.get();
    item.blend = item.material ? item.material->blend : BlendMode::Additive;
    item.depth_write = item.material ? item.material->DepthWrite() : false;
    if (texture) item.texture = GLTexture(*texture);
    const auto index = static_cast<uint32_t>(draw_items_.size());
    draw_items_.push_back(item);
    const uint64_t key = item.blend == BlendMode::Opaque ? OpaqueSortKey(program.id, item.texture, depth)
                                                         : TransparentSortKey(depth, program.id);
    draw_order_.push_back({key, index});
  }

  // --- 4. SHADOWS: every caster's depth, seen from the shadow light ---
  void DrawShadowMap(const Frame& frame) {
    WLW_PROFILE_SCOPE("Shadow pass");
    WLW_PROFILE_GPU_ZONE("Shadow pass");
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
      scene::GeometryInstance3D* node = drawable.node;
      if (!node->CastsShadow()) continue;
      const ShadowProgram& program = ShadowProgramFor(IsMultiMesh(*node));
      if (program.id == 0) continue;
      state_.UseProgram(program.id);
      glUniformMatrix4fv(program.model, 1, GL_FALSE, glm::value_ptr(node->GetWorldMatrix()));
      GLInstanceBuffer* instances = PrepareInstances(*node);
      for (const auto& mesh : node->GetModel()->meshes) DrawMesh(*node, *mesh, instances, nullptr);
    }
    glDisable(GL_POLYGON_OFFSET_FILL);
    glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(previous_framebuffer));
  }

  // The sky (sky.h): one triangle at depth 1.0, drawn where the depth buffer still holds the clear
  // value (LEQUAL), or everywhere into a target without depth. No depth writes.
  void DrawSky(const WCubemap& skybox) {
    WLW_PROFILE_SCOPE("Sky");
    state_.SetDepthWrite(false);
    state_.SetBlendMode(BlendMode::Opaque);
    state_.UseProgram(skybox_program_);
    const glm::mat4 ray = SkyRayMatrix(scene_view_, scene_projection_);
    glUniformMatrix4fv(skybox_ray_, 1, GL_FALSE, glm::value_ptr(ray));
    BindGLTexture(skybox, 0);  // a cubemap target: unit 0's 2D texture (the cache's) stays bound
    glDepthFunc(GL_LEQUAL);
    glBindVertexArray(post_vao_);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glBindVertexArray(0);
    glDepthFunc(GL_LESS);
  }

  // --- 5. MAIN PASS: the draw list in key order, the sky after the opaque draws ---
  void DrawMainPass(const scene::Scene& scene, const Frame& frame, bool has_depth, scene::RenderStats& stats) {
    WLW_PROFILE_SCOPE("Main pass");
    WLW_PROFILE_GPU_ZONE("Main pass");
    // The shadow map on texture unit 1 (unit 0: the mesh's texture; units 2+: ShaderMaterials').
    if (frame.shadow_active) {
      glActiveTexture(GL_TEXTURE1);
      glBindTexture(GL_TEXTURE_2D, shadow_texture_);
      glActiveTexture(GL_TEXTURE0);
    }

    const WCubemap* skybox = scene.GetSkybox().get();
    const size_t sky_position = skybox ? SkyDrawPosition(draw_order_, has_depth) : draw_order_.size() + 1;
    for (size_t i = 0; i < draw_order_.size(); ++i) {
      if (i == sky_position) DrawSky(*skybox);
      const DrawOrder& order = draw_order_[i];
      const DrawItem& item = draw_items_[order.item];
      const NodeSlot& slot = node_slots_[item.node_slot];
      state_.SetBlendMode(item.blend);
      state_.SetDepthWrite(item.depth_write);
      if (IsTransparentKey(order.key)) ++stats.transparent_draw_calls;
      const glm::mat4& model = slot.model;
      if (item.quads) {
        DrawQuads(item, model, stats);
        continue;
      }
      if (item.custom) {
        UseShaderMaterial(*item.custom, *item.custom_program, slot, *item.node, IsMultiMesh(*item.node),
                          frame.shadow_active && item.node->ReceivesShadow());
      } else {
        const MeshProgram& program = *item.program;
        state_.UseProgram(program.id);
        glUniformMatrix4fv(program.model, 1, GL_FALSE, glm::value_ptr(model));
        glUniformMatrix3fv(program.normal_matrix, 1, GL_FALSE, glm::value_ptr(slot.normal_matrix));
        glUniform1f(program.shininess, item.material ? item.material->shininess : 32.0f);
        if (item.texture) state_.BindTexture0(item.texture);
      }
      DrawMesh(*item.node, *item.mesh, slot.instances, &stats);
    }
    if (sky_position == draw_order_.size()) DrawSky(*skybox);  // nothing transparent

    state_.SetBlendMode(BlendMode::Opaque);  // back to the defaults for whatever draws next
    state_.SetDepthWrite(true);
  }

  // The draw's features, which pick its shader variant.
  template <typename MeshT>
  static MeshDraw DrawFor(const scene::GeometryInstance3D& node, const MeshT& mesh, const Frame& frame) {
    MeshDraw draw;
    draw.instanced = IsMultiMesh(node);
    draw.textured = TexturedMaterial(node, mesh) != nullptr;
    draw.scene_lit = !frame.lights.empty();
    draw.shadow_active = frame.shadow_active;
    draw.receives_shadow = node.ReceivesShadow();
    draw.blends = EffectiveMaterialBlend(node, mesh) != BlendMode::Opaque;
    return draw;
  }

  // The material that decides a mesh's look: its own, else its node's (the mesh's wins).
  template <typename MeshT>
  static const Material* EffectiveMaterial(const scene::GeometryInstance3D& node, const MeshT& mesh) {
    if (const auto& material = mesh.GetMaterial()) return material.get();
    return node.GetMaterial().get();
  }
  // The material whose texture the mesh shows: its own if it has one, else its node's.
  template <typename MeshT>
  static const Material* TexturedMaterial(const scene::GeometryInstance3D& node, const MeshT& mesh) {
    if (const auto& material = mesh.GetMaterial(); material && material->HasTexture()) return material.get();
    if (const auto& material = node.GetMaterial(); material && material->HasTexture()) return material.get();
    return nullptr;
  }
  template <typename MeshT>
  static BlendMode EffectiveMaterialBlend(const scene::GeometryInstance3D& node, const MeshT& mesh) {
    const Material* material = EffectiveMaterial(node, mesh);
    return material ? material->blend : BlendMode::Opaque;
  }
  template <typename MeshT>
  static bool EffectiveDepthWrite(const scene::GeometryInstance3D& node, const MeshT& mesh) {
    const Material* material = EffectiveMaterial(node, mesh);
    return material ? material->DepthWrite() : true;
  }

  static bool IsMultiMesh(const scene::GeometryInstance3D& node) {
    return node.Kind() == scene::NodeKind::MultiMeshInstance;
  }

  // A multi-mesh's per-copy data on the GPU (uploaded only when it changed: Upload compares
  // versions); null for a MeshInstance3D.
  static GLInstanceBuffer* PrepareInstances(scene::GeometryInstance3D& node, scene::RenderStats* stats = nullptr) {
    if (!IsMultiMesh(node)) return nullptr;
    auto& multimesh = static_cast<scene::MultiMeshInstance3D&>(node);
    if (!multimesh.Internal().GetInstanceBuffer())
      multimesh.Internal().SetInstanceBuffer(std::make_unique<GLInstanceBuffer>());
    auto* buffer = static_cast<GLInstanceBuffer*>(multimesh.Internal().GetInstanceBuffer());
    const size_t bytes = buffer->Upload(multimesh.Instances(), multimesh.Internal().GetInstancesVersion());
    if (stats) stats->instance_bytes += static_cast<int>(bytes);
    return buffer;
  }

  struct DrawItem;  // (below)
  // A sprite or billboard batch: its quads to the GPU if they changed (16 / 40 bytes each), then one instanced call.
  void DrawQuads(const DrawItem& item, const glm::mat4& model, scene::RenderStats& stats) {
    const GLQuadBuffer* buffer = nullptr;
    GLsizei count = 0;
    if (item.quad_shader == QuadShader::Sprite) {
      auto& batch = static_cast<scene::SpriteBatch&>(*item.quads);
      if (!batch.Internal().GetSpriteBuffer())
        batch.Internal().SetSpriteBuffer(std::make_unique<GLSpriteBuffer>(batch.Capacity()));
      auto* sprites = static_cast<GLSpriteBuffer*>(batch.Internal().GetSpriteBuffer());
      stats.instance_bytes += static_cast<int>(sprites->Upload(batch.Sprites(), batch.Internal().Version()));
      buffer = sprites;
      count = static_cast<GLsizei>(batch.Count());
    } else {
      auto& batch = static_cast<scene::BillboardBatch&>(*item.quads);
      if (!batch.Internal().GetBuffer())
        batch.Internal().SetBuffer(std::make_unique<GLBillboardBuffer>(batch.Capacity()));
      auto* billboards = static_cast<GLBillboardBuffer*>(batch.Internal().GetBuffer());
      stats.instance_bytes += static_cast<int>(billboards->Upload(batch.Billboards(), batch.Internal().Version()));
      buffer = billboards;
      count = static_cast<GLsizei>(batch.Count());
    }
    state_.UseProgram(item.quad_program->id);
    glUniformMatrix4fv(item.quad_program->model, 1, GL_FALSE, glm::value_ptr(model));
    if (item.texture) state_.BindTexture0(item.texture);
    buffer->Draw(count);
    stats.instances += count;
    ++stats.draw_calls;
  }

  // The texture shaping a batch's quads (its material's), else null: the shader's own falloff.
  static const WTexture* MaterialTexture(const std::shared_ptr<Material>& material) {
    return material && material->HasTexture() ? material->GetTexture().get() : nullptr;
  }

  // Draws one mesh with whatever program is in use: once, or once per copy (one call either way).
  template <typename MeshT>
  void DrawMesh(scene::GeometryInstance3D& node, MeshT& mesh, GLInstanceBuffer* instances, scene::RenderStats* stats) {
    if (!mesh.Internal().GetVertexBuffer())
      mesh.Internal().SetVertexBuffer(device_->CreateVertexBuffer(mesh.GetVertices()));
    if (!mesh.Internal().GetIndexBuffer())
      mesh.Internal().SetIndexBuffer(device_->CreateIndexBuffer(mesh.GetIndices()));
    const auto* vertex_buffer = mesh.Internal().GetVertexBuffer();
    const auto* index_buffer = mesh.Internal().GetIndexBuffer();
    vertex_buffer->Bind();
    index_buffer->Bind();
    const auto index_count = static_cast<GLsizei>(mesh.GetIndices().size());
    if (instances) {
      const auto count = static_cast<GLsizei>(static_cast<scene::MultiMeshInstance3D&>(node).InstanceCount());
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

  // A ShaderMaterial's shader with its node's matrices, whether it's shadowed (SceneShadow in
  // wlw/shadows.glsl: the frame has a shadow light and the node receives it), its params (the
  // camera and lights come from the frame block) and the node's instance params. Its textures start at unit 2:
  // units 0-1 are the mesh texture and the shadow map, which stay bound for the nodes drawn after.
  struct NodeSlot;  // (below)
  void UseShaderMaterial(const ShaderMaterial& material, const GLShader& shader, const NodeSlot& slot,
                         const scene::GeometryInstance3D& node, bool instanced, bool shadowed) {
    state_.UseProgram(shader.Program());
    const GLShader::Inputs& inputs = shader.EngineInputs();
    glUniformMatrix4fv(inputs.model, 1, GL_FALSE, glm::value_ptr(slot.model));
    glUniformMatrix3fv(inputs.normal_matrix, 1, GL_FALSE, glm::value_ptr(slot.normal_matrix));
    glUniform1i(inputs.instanced, instanced);
    glUniform1i(inputs.receives_shadow, shadowed);
    glUniform1i(inputs.shadow_map, 1);  // (-1, ignored, unless it includes wlw/shadows.glsl)
    shader.ApplyParams(material, 2);
    shader.ApplyInstanceParams(node.GetInstanceParams());
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
  using QuadProgram = ShadowProgram;  // also just a program and its uModel

  // One mesh to draw this frame (main pass), made by BuildDrawList; drawn in the order of its key.
  struct DrawItem {
    scene::GeometryInstance3D* node = nullptr;  // the mesh's node (null for quads)
    core::Mesh<core::Vertex3D>* mesh = nullptr;
    uint32_t node_slot = 0;                       // its node's per-frame data (node_slots_)
    const Material* material = nullptr;           // the material deciding its look (mesh's, else node's)
    const ShaderMaterial* custom = nullptr;       // drawn with this material's shader...
    const GLShader* custom_program = nullptr;     // (its program)
    const MeshProgram* program = nullptr;         // ...or with this engine variant
    scene::Node3D* quads = nullptr;               // a sprite / billboard batch instead of a mesh (mesh is null)...
    QuadShader quad_shader = QuadShader::Sprite;  // (which of the two)
    const QuadProgram* quad_program = nullptr;    // ...with its quad program
    GLuint texture = 0;                           // engine variant / quads: its texture on unit 0 (0 = none)
    BlendMode blend = BlendMode::Opaque;
    bool depth_write = true;
  };
  // What a node's meshes share, worked out once per frame.
  struct NodeSlot {
    scene::GeometryInstance3D* node = nullptr;  // null for a sprite / billboard batch
    glm::mat4 model{1.0f};                      // world matrix (walks the parents once)
    glm::mat3 normal_matrix{1.0f};              // core::NormalMatrix(model) (instanced: the node's part)
    GLInstanceBuffer* instances = nullptr;      // instanced nodes (set just before drawing)
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
      WLW_LOG(LogRender, Info, "Shader variant compiled during play (%.1f ms):%s", ms, VariantName(key).c_str());
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

  // A sprite / billboard batches' program: plain (the dot computed) or TEXTURED (the shape from its texture).
  const QuadProgram& QuadProgramFor(QuadShader shader, bool textured) {
    const int kind = shader == QuadShader::Sprite ? 0 : 1;
    QuadProgram& program = quad_programs_[kind][textured ? 1 : 0];
    if (program.id != 0 || quad_failed_[kind][textured ? 1 : 0]) return program;
    const auto start = std::chrono::steady_clock::now();
    const char* name = kind == 0 ? "sprite" : "billboard";
    program.id =
        BuildEngineProgram(kind == 0 ? "sprite.vert" : "billboard.vert", kind == 0 ? "sprite.frag" : "billboard.frag",
                           textured ? std::vector<std::string>{"TEXTURED"} : std::vector<std::string>{});
    if (program.id == 0) {
      quad_failed_[kind][textured ? 1 : 0] = true;  // logged once, not retried every frame
      return program;
    }
    program.model = glGetUniformLocation(program.id, "uModel");
    if (textured) {
      state_.UseProgram(program.id);
      glUniform1i(glGetUniformLocation(program.id, "uTexture"), 0);
    }
    if (warmed_up_) {
      const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
      WLW_LOG(LogRender, Info, "Quad shader compiled during play (%.1f ms): %s%s", ms, name,
              textured ? " TEXTURED" : "");
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
      WLW_LOG(LogRender, Error, "Shadow map framebuffer incomplete: shadows are off");
      DeleteShadowMap();
      shadow_failed_ = true;
      return false;
    }
    shadow_map_size_ = map_size;
    WLW_LOG(LogRender, Info, "Shadow map: %d x %d", map_size, map_size);
    return true;
  }

  void DeleteShadowMap() {
    if (shadow_framebuffer_) glDeleteFramebuffers(1, &shadow_framebuffer_);
    if (shadow_texture_) glDeleteTextures(1, &shadow_texture_);
    shadow_framebuffer_ = 0;
    shadow_texture_ = 0;
    shadow_map_size_ = 0;
  }

  ~GLRenderingDriver() override {
    if (!platform_started_ || !platform::IsAlive()) return;
    // Free the GL objects while the main window's context still exists: its scenes and viewports (their
    // meshes, textures and targets, unless something else still holds them), then our own...
    if (main_window_) {
      main_window_->MakeContextCurrent();
      main_window_->Internal().ReleaseScenes();
      ui_renderer_.reset();
      for (const auto& [_, program] : mesh_programs_) {
        if (program.id) glDeleteProgram(program.id);
      }
      for (const ShadowProgram& program : shadow_programs_) {
        if (program.id) glDeleteProgram(program.id);
      }
      for (const auto& programs : quad_programs_) {
        for (const QuadProgram& program : programs) {
          if (program.id) glDeleteProgram(program.id);
        }
      }
      if (skybox_program_) glDeleteProgram(skybox_program_);
      if (frame_ubo_) glDeleteBuffers(1, &frame_ubo_);
      DeleteShadowMap();
      glDeleteVertexArrays(1, &post_vao_);
      copy_shader_.reset();
      output_pass_.material.reset();
      for (PostPass& pass : bloom_down_) pass.material.reset();
      for (PostPass& pass : bloom_up_) pass.material.reset();
      gpu_timer_.reset();
    }
    main_window_.reset();
    // ...then our hold on the platform: the last one shuts it down (destroys any windows / contexts still open).
    platform::Shutdown();
  }

 private:
  RenderDevice* device_;
  bool platform_started_ = false;  // Initialize took a platform::Init() (counted): the destructor gives it back
  std::string gpu_name_;           // GL_RENDERER
  std::shared_ptr<scene::Window> main_window_;
  std::unique_ptr<ui::UIRenderer> ui_renderer_;

  std::unordered_map<uint32_t, MeshProgram> mesh_programs_;  // by variant key
  ShadowProgram shadow_programs_[2];                         // plain, instanced
  QuadProgram quad_programs_[2][2];                          // [sprite, billboard][computed dot, textured]
  bool quad_failed_[2][2] = {};
  GLuint skybox_program_ = 0;
  GLint skybox_ray_ = -1;                     // its uSkyRay
  GLuint post_vao_ = 0;                       // empty: post passes draw a triangle made in the shader
  std::shared_ptr<Shader> copy_shader_;       // a post pass without a usable shader
  PostPass upscale_pass_{.name = "Upscale"};  // kUpscalePass: no material, so a copy
  PostPass output_pass_{.name = "Output"};    // kOutputPass: OutputMaterial()
  PostPass bloom_down_[kMaxBloomLevels];      // kBloomDownPass, per level: MakeBloomPasses()
  PostPass bloom_up_[kMaxBloomLevels];        // kBloomUpPass, per level
  int max_target_size_ = 0;                   // the largest side the GPU draws into (0: not known)
  bool warned_render_size_ = false;
  std::unique_ptr<GpuTimer> gpu_timer_;  // null: this GPU can't time
  bool invalidate_ = true;               // Window::InvalidateFramebuffers, for this frame
  bool timing_ = false;                  // timing this frame
  std::vector<GpuSample> gpu_samples_;   // results arriving this frame
  std::vector<float> raw_pass_ms_;       // scratch for ApplyGpuSamples
  GLuint frame_ubo_ = 0;
  GLStateCache state_;  // program, blend, depth writes, unit-0 texture: skips redundant GL calls
  // Per-viewport lists, reused every frame (cleared, never shrunk: no allocations once warm).
  Frame frame_;
  std::vector<DrawItem> draw_items_;
  std::vector<NodeSlot> node_slots_;
  VisibleSet visible_;  // CullScene's output, reused every viewport
  std::vector<DrawOrder> draw_order_;
  bool warmed_up_ = false;

  // This viewport's camera (set by SetCameraMatrices), for culling, sorting, the frame block and the sky.
  glm::mat4 scene_view_{1.0f};
  glm::mat4 scene_projection_{1.0f};
  glm::mat4 scene_view_projection_{1.0f};
  uint32_t scene_cull_mask_ = 0xFFFFFFFFu;  // the camera's layers

  // Shadows (see DrawShadowMap).
  GLuint shadow_framebuffer_ = 0;
  GLuint shadow_texture_ = 0;
  int shadow_map_size_ = 0;
  bool shadow_failed_ = false;  // the framebuffer was refused once: don't retry every frame
  bool warned_shadow_type_ = false;
};

std::unique_ptr<RenderingDriver> RenderingDriver::Create(RenderDevice* device) {
  return std::make_unique<GLRenderingDriver>(device);
}

}  // namespace wlw::rendering

#endif  // WLW_RENDERER_GL
