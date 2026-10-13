#pragma once

#include <memory>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include <glm/glm.hpp>

#include "wlw/rendering/render_target.h"
#include "wlw/rendering/shader_material.h"
#include "wlw/rendering/texture.h"

// Post passes: full-screen shaders run on a viewport's finished 3D picture, each one reading what
// the one before wrote (blur, threshold, bloom, colour grading...). Like Unity's renderer features,
// Godot 4's CompositorEffect and Unreal's post-process materials. Ready-made ones: scene/post_effects.h.
//
//   viewport.AddPostPass({.material = my_material});   // a material with a `shader_type post;` shader
//
// The order of a frame, per viewport: its 3D into a texture of its own (the "scene picture"), the
// passes in the order added, the LAST one writing where the viewport's picture goes (the screen or
// the viewport's texture), then its 2D canvas on top (the UI stays sharp). The passes before the
// last write into textures the viewport keeps for them (a small pool, reused every frame).
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
  TextureFormat format = TextureFormat::RGBA8;  // RGBA16F: values above 1 (glow); ignored by the last pass
  std::string name;                          // lets later passes read its output (PostInput)
  std::vector<PostInput> inputs;
};

// ---- The plan (CPU only, testable): which texture each pass reads and writes ----

inline constexpr int kSceneTarget = -1;  // the scene picture
inline constexpr int kDestination = -2;  // where the viewport's picture goes (the last pass's output)

struct PostTargetSpec {
  glm::ivec2 size{1, 1};
  TextureFormat format = TextureFormat::RGBA8;
};

struct PostStep {
  int pass = 0;                // index into the passes
  int source = kSceneTarget;   // read as uSource: kSceneTarget or a target index
  int output = kDestination;   // written: kDestination or a target index
  glm::ivec2 size{1, 1};       // the output's size in pixels
  std::vector<std::pair<std::string, int>> inputs;  // extra textures: uniform -> kSceneTarget or target index
};

struct PostPlan {
  std::vector<PostStep> steps;           // one per pass, in order
  std::vector<PostTargetSpec> targets;   // the textures the passes before the last need
  std::vector<std::string> errors;       // unknown input names, repeated names (those inputs are skipped)
};

// Plans the passes for a viewport of `pixels`. Targets are shared as much as possible: a pass
// writes into a target of its size and format that no later pass still has to read (passes
// ping-pong between two), and never into the one it reads.
PostPlan PlanPostChain(std::span<const PostPass> passes, glm::ivec2 pixels);

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
  bool Empty() const { return passes_.empty(); }
  const std::vector<PostPass>& Passes() const { return passes_; }
  // Gives a pass a name (e.g. so an effect added after it can read its output).
  void SetName(size_t index, std::string name) {
    if (index >= passes_.size()) return;
    passes_[index].name = std::move(name);
    planned_ = false;
  }

  // ---- For the renderer ----
  // The plan for a viewport of `pixels`, and the textures it needs at their sizes (re-planned only
  // when the passes or the size changed; errors are logged then). The scene picture's target gets
  // `scene` (format, smooth, depth).
  const PostPlan& Prepare(glm::ivec2 pixels, const RenderTargetSettings& scene);
  RenderTarget& SceneTarget() { return *scene_target_; }
  RenderTarget& Target(int index) { return *targets_[static_cast<size_t>(index)]; }
  // A step's texture: the scene picture or a target.
  const std::shared_ptr<WTexture2D>& TextureOf(int index) {
    return index == kSceneTarget ? scene_target_->GetTexture() : targets_[static_cast<size_t>(index)]->GetTexture();
  }
  int TargetCount() const { return static_cast<int>(targets_.size()); }

 private:
  std::vector<PostPass> passes_;
  PostPlan plan_;
  bool planned_ = false;
  glm::ivec2 planned_pixels_{0, 0};
  std::shared_ptr<RenderTarget> scene_target_;
  RenderTargetSettings scene_settings_;           // as asked for (the GPU may fall back)
  std::vector<std::shared_ptr<RenderTarget>> targets_;
  std::vector<TextureFormat> target_formats_;     // as asked for
};

}  // namespace wlw::rendering
