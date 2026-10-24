#pragma once

#include <memory>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include <glm/glm.hpp>

#include "wlw/rendering/post_settings.h"
#include "wlw/rendering/render_target.h"
#include "wlw/rendering/shader_material.h"
#include "wlw/rendering/texture.h"

// Post passes: full-screen shaders run on a viewport's finished 3D picture, each one reading what
// the one before wrote (blur, threshold, bloom, colour grading...). Like Unity's renderer features,
// Godot 4's CompositorEffect and Unreal's post-process materials. Ready-made ones: scene/post_effects.h.
//
//   viewport.AddPostPass({.material = my_material});   // a material with a `shader_type post;` shader
//
// The order of a frame, per viewport: its 3D into a texture of its own (the "scene picture", at the
// viewport's render size: Viewport::SetRenderScale), the passes in the order added, the LAST one
// writing where the viewport's picture goes (the screen or the viewport's texture), then its 2D
// canvas on top (the UI stays sharp). When the settings need the output pass (post_settings.h:
// tone mapping...), it runs after the game's passes and is the one that writes the destination.
// The passes before the last write into textures the viewport
// keeps for them (a small pool, reused every frame). Pass sizes are fractions of the viewport's
// size, whatever its render scale: passes read their inputs with linear filtering, so a smaller
// scene picture is stretched by the first pass that reads it.
namespace wlw::rendering {

// The name an input uses for the scene picture (the viewport's 3D before any pass).
inline constexpr const char* kScenePicture = "scene";

// A texture a pass reads besides uSource and uScene: the output of an earlier pass, by name.
struct PostInput {
  std::string uniform;  // the shader's `uniform sampler2D <uniform>;`
  std::string pass;     // the earlier pass's name, or kScenePicture
};

struct PostPass {
  std::shared_ptr<ShaderMaterial> material;  // its shader must be `shader_type post;`; its params apply
                                             // (null or not compiling: the pass copies uSource)
  float scale = 1.0f;                        // output size as a fraction of the viewport's: 0.25 = a
                                             // cheap blur (the last pass is always full size)
  std::optional<TextureFormat> format;       // nullopt = the scene picture's (an HDR viewport's passes
                                             // stay HDR); RGBA16F: values above 1. Ignored by the last pass
  std::string name;                          // lets later passes read its output (PostInput)
  std::vector<PostInput> inputs;
};

// ---- The plan (CPU only, testable): which texture each pass reads and writes ----

inline constexpr int kSceneTarget = -1;  // the scene picture
inline constexpr int kDestination = -2;  // where the viewport's picture goes (the last pass's output)
inline constexpr int kUpscalePass = -1;  // PostStep::pass of the copy that brings the scene picture
                                         // to the destination (stretched if scaled) without passes
inline constexpr int kOutputPass = -2;   // PostStep::pass of the output pass (post_settings.h): after
                                         // the passes, writing the destination

struct PostTargetSpec {
  glm::ivec2 size{1, 1};
  TextureFormat format = TextureFormat::RGBA8;
};

struct PostStep {
  int pass = 0;                                     // index into the passes, or kUpscalePass
  int source = kSceneTarget;                        // read as uSource: kSceneTarget or a target index
  int output = kDestination;                        // written: kDestination or a target index
  glm::ivec2 size{1, 1};                            // the output's size in pixels
  std::vector<std::pair<std::string, int>> inputs;  // extra textures: uniform -> kSceneTarget or target index
};

struct PostPlanOptions {
  TextureFormat scene_format = TextureFormat::RGBA8;  // the scene picture's: passes without a format write it
  bool output_pass = false;                           // one more step after the passes (kOutputPass)
};

struct PostPlan {
  std::vector<PostStep> steps;          // one per pass, in order (then the output pass, if planned)
  std::vector<PostTargetSpec> targets;  // the textures the passes before the last need
  std::vector<std::string> errors;      // unknown input names, repeated names (those inputs are skipped)
};

// Plans the passes for a viewport of `pixels` whose scene picture is `scene_pixels` big (its render
// size; {0, 0} = `pixels`). Targets are shared as much as possible: a pass writes into a target of
// its size and format that no later pass still has to read (passes ping-pong between two), and
// never into the one it reads. Without passes, a scene picture of another size gets one step: a
// copy (kUpscalePass) that stretches it to `pixels`. With `options.output_pass` the output pass is the
// last step (full size, writing the destination; it also does the stretch) and every pass writes a target.
PostPlan PlanPostChain(std::span<const PostPass> passes, glm::ivec2 pixels, glm::ivec2 scene_pixels = {0, 0},
                       const PostPlanOptions& options = {});

// A viewport's passes and the textures they use (owned by scene::Viewport).
class PostChain {
 public:
  void Add(PostPass pass) {
    passes_.push_back(std::move(pass));
    planned_ = false;
  }
  void Clear() {
    passes_.clear();
    planned_ = false;
  }
  bool Empty() const { return passes_.empty(); }  // no game passes (the output pass may still run)
  const std::vector<PostPass>& Passes() const { return passes_; }

  // The viewport's post settings (post_settings.h): change them any time, read every frame.
  PostSettings& Settings() { return settings_; }
  const PostSettings& Settings() const { return settings_; }
  // Whether the output pass runs (re-planned before the next draw when this changes).
  bool HasOutputPass() const { return settings_.NeedsOutputPass(); }
  // Whether anything runs after the 3D: game passes or the output pass.
  bool HasWork() const { return !passes_.empty() || HasOutputPass(); }
  // Gives a pass a name (e.g. so an effect added after it can read its output).
  void SetName(size_t index, std::string name) {
    if (index >= passes_.size()) return;
    passes_[index].name = std::move(name);
    planned_ = false;
  }

  // Changes a pass's output size (e.g. for a quality level): re-planned before the next draw,
  // the textures keep their formats and are only resized.
  void SetScale(size_t index, float scale) {
    if (index >= passes_.size() || passes_[index].scale == scale) return;
    passes_[index].scale = scale;
    planned_ = false;
  }

  class InternalAccess;
  // Engine-only parts (the renderer's): see InternalAccess below.
  InternalAccess Internal();

 private:
  // The plan for a viewport of `pixels`, and the textures it needs at their sizes (re-planned only
  // when the passes or a size changed; errors are logged then). The scene picture's target gets
  // `scene` (format, smooth, depth) and `scene_pixels` (the render size; {0, 0} = `pixels`). The plan
  // always reaches the destination: without passes it is one copy (kUpscalePass), even at the same
  // size (the renderer asks then only for a screen without depth, which draws through the picture).
  const PostPlan& Prepare(glm::ivec2 pixels, const RenderTargetSettings& scene, glm::ivec2 scene_pixels = {0, 0});
  RenderTarget& SceneTarget() { return *scene_target_; }
  RenderTarget& Target(int index) { return *targets_[static_cast<size_t>(index)]; }
  // A step's texture: the scene picture or a target.
  const std::shared_ptr<WTexture2D>& TextureOf(int index) {
    return index == kSceneTarget ? scene_target_->GetTexture() : targets_[static_cast<size_t>(index)]->GetTexture();
  }
  int TargetCount() const { return static_cast<int>(targets_.size()); }

  std::vector<PostPass> passes_;
  PostSettings settings_;
  PostPlan plan_;
  bool planned_ = false;
  glm::ivec2 planned_pixels_{0, 0};
  glm::ivec2 planned_scene_pixels_{0, 0};
  PostPlanOptions planned_options_;
  std::shared_ptr<RenderTarget> scene_target_;
  RenderTargetSettings scene_settings_;  // as asked for (the GPU may fall back)
  std::vector<std::shared_ptr<RenderTarget>> targets_;
  std::vector<TextureFormat> target_formats_;  // as asked for
};

// For the renderer, not games (see the private members of the same names for what they do).
class PostChain::InternalAccess {
 public:
  explicit InternalAccess(PostChain& chain) : chain_(chain) {}

  const PostPlan& Prepare(glm::ivec2 pixels, const RenderTargetSettings& scene,
                          glm::ivec2 scene_pixels = {0, 0}) const {
    return chain_.Prepare(pixels, scene, scene_pixels);
  }
  RenderTarget& SceneTarget() const { return chain_.SceneTarget(); }
  RenderTarget& Target(int index) const { return chain_.Target(index); }
  const std::shared_ptr<WTexture2D>& TextureOf(int index) const { return chain_.TextureOf(index); }
  int TargetCount() const { return chain_.TargetCount(); }

 private:
  PostChain& chain_;
};

inline PostChain::InternalAccess PostChain::Internal() { return InternalAccess(*this); }

}  // namespace wlw::rendering
