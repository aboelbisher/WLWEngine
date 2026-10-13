// Tests for post passes (rendering/post_pass.h: planning which texture each pass reads and writes,
// the chain's targets) and the ready-made effects (scene/post_effects.h). CPU only: materials,
// shaders and targets are made without a GPU.
#include <memory>
#include <regex>
#include <set>
#include <string>
#include <vector>

#include "check.h"
#include "wlw/rendering/post_pass.h"
#include "wlw/rendering/shader.h"
#include "wlw/rendering/shader_library.h"
#include "wlw/scene/post_effects.h"
#include "wlw/scene/viewport.h"

using namespace wlw;
using rendering::kDestination;
using rendering::kSceneTarget;
using rendering::PostPass;
using rendering::PostPlan;
using rendering::TextureFormat;

namespace {

const glm::ivec2 kPhone{1080, 2400};

PostPass Pass(float scale = 1.0f, std::string name = "", std::vector<rendering::PostInput> inputs = {},
              TextureFormat format = TextureFormat::RGBA8) {
  return PostPass{.scale = scale, .format = format, .name = std::move(name), .inputs = std::move(inputs)};
}

// Plays the plan and checks the rules every step must keep: it never writes a texture it reads,
// and nothing a later step still needs is overwritten (the value each read finds is the one the
// plan meant: the scene, or the output of the pass it names).
void CheckPlanIsSafe(const std::vector<PostPass>& passes, const PostPlan& plan) {
  CHECK(plan.steps.size() == passes.size());
  std::vector<int> holds(plan.targets.size(), -2);  // per target: which pass's output it holds now
  for (size_t i = 0; i < plan.steps.size(); ++i) {
    const rendering::PostStep& step = plan.steps[i];
    CHECK(step.pass == static_cast<int>(i));
    // uSource: the previous pass's output, still there.
    if (i == 0) {
      CHECK(step.source == kSceneTarget);
    } else {
      CHECK(step.source >= 0 && holds[static_cast<size_t>(step.source)] == static_cast<int>(i) - 1);
    }
    for (const auto& [uniform, texture] : step.inputs) {
      CHECK(texture != step.output);
      if (texture >= 0) CHECK(holds[static_cast<size_t>(texture)] >= 0);  // some earlier pass's output, kept
    }
    CHECK(step.output != step.source);
    if (step.output >= 0) {
      CHECK(plan.targets[static_cast<size_t>(step.output)].size == step.size);
      holds[static_cast<size_t>(step.output)] = static_cast<int>(i);
    }
  }
  if (!plan.steps.empty()) CHECK(plan.steps.back().output == kDestination);
}

}  // namespace

static void TestEmptyAndSingle() {
  CHECK(rendering::PlanPostChain({}, kPhone).steps.empty());

  // One pass reads the scene picture and writes the destination: no extra textures.
  const std::vector<PostPass> one{Pass(0.25f)};
  const PostPlan plan = rendering::PlanPostChain(one, kPhone);
  CHECK(plan.steps.size() == 1 && plan.targets.empty());
  CHECK(plan.steps[0].source == kSceneTarget && plan.steps[0].output == kDestination);
  CHECK(plan.steps[0].size == kPhone);  // the last pass is always full size
}

static void TestPingPong() {
  // A chain of full-size passes needs only two textures, used in turn.
  const std::vector<PostPass> passes{Pass(), Pass(), Pass(), Pass(), Pass()};
  const PostPlan plan = rendering::PlanPostChain(passes, kPhone);
  CHECK(plan.targets.size() == 2);
  CHECK(plan.steps[0].output == 0 && plan.steps[1].output == 1 && plan.steps[2].output == 0 &&
        plan.steps[3].output == 1);
  CheckPlanIsSafe(passes, plan);
}

static void TestSizesAndFormats() {
  const std::vector<PostPass> passes{Pass(0.5f), Pass(0.25f), Pass(0.25f, "", {}, TextureFormat::RGBA16F),
                                     Pass(0.25f)};
  const PostPlan plan = rendering::PlanPostChain(passes, kPhone);
  CHECK(plan.steps[0].size == glm::ivec2(540, 1200));
  CHECK(plan.steps[1].size == glm::ivec2(270, 600));
  // Different formats never share a texture.
  CHECK(plan.steps[2].output != plan.steps[1].output);
  CHECK(plan.targets[static_cast<size_t>(plan.steps[2].output)].format == TextureFormat::RGBA16F);
  CHECK(plan.steps[3].size == kPhone);
  CheckPlanIsSafe(passes, plan);

  // Never below one pixel.
  const std::vector<PostPass> tiny{Pass(0.0f), Pass()};
  CHECK(rendering::PlanPostChain(tiny, {3, 3}).steps[0].size == glm::ivec2(1, 1));
}

static void TestNamedOutputsAreKept() {
  // Pass 3 reads pass 0's output: nothing in between may overwrite it.
  const std::vector<PostPass> passes{Pass(1.0f, "base"), Pass(), Pass(), Pass(1.0f, "", {{"uBase", "base"}})};
  const PostPlan plan = rendering::PlanPostChain(passes, kPhone);
  CHECK(plan.errors.empty());
  const int base = plan.steps[0].output;
  CHECK(plan.steps[1].output != base && plan.steps[2].output != base);
  CHECK(plan.steps[3].inputs.size() == 1 && plan.steps[3].inputs[0].first == "uBase" &&
        plan.steps[3].inputs[0].second == base);
  CHECK(plan.targets.size() == 3);
  CheckPlanIsSafe(passes, plan);

  // Once its last reader is done, the texture is free again.
  const std::vector<PostPass> later{Pass(1.0f, "a"), Pass(1.0f, "", {{"uA", "a"}}), Pass(), Pass(), Pass()};
  const PostPlan reused = rendering::PlanPostChain(later, kPhone);
  CHECK(reused.targets.size() == 2);
  CheckPlanIsSafe(later, reused);
}

static void TestInputErrors() {
  // The scene picture by name; an unknown name and a name used twice are reported.
  const std::vector<PostPass> passes{Pass(1.0f, "x", {{"uFirst", rendering::kScenePicture}}), Pass(1.0f, "x"),
                                     Pass(1.0f, "", {{"uX", "x"}, {"uMissing", "nope"}})};
  const PostPlan plan = rendering::PlanPostChain(passes, kPhone);
  CHECK(plan.steps[0].inputs.size() == 1 && plan.steps[0].inputs[0].second == kSceneTarget);
  CHECK(plan.errors.size() == 2);
  // "x" means the latest pass before the reader with that name (pass 1); the unknown input is skipped.
  CHECK(plan.steps[2].inputs.size() == 1 && plan.steps[2].inputs[0].second == plan.steps[1].output);
  CheckPlanIsSafe(passes, plan);

  // Reading a later pass isn't possible (it hasn't run yet).
  const std::vector<PostPass> forward{Pass(1.0f, "", {{"uLater", "later"}}), Pass(1.0f, "later")};
  CHECK(rendering::PlanPostChain(forward, kPhone).errors.size() == 1);
}

static void TestChainTargets() {
  rendering::PostChain chain;
  chain.Add(Pass(0.5f));
  chain.Add(Pass(0.5f));
  chain.Add(Pass());
  const rendering::RenderTargetSettings scene{TextureFormat::RGBA8, true, /*depth=*/true};
  const PostPlan& plan = chain.Prepare({200, 100}, scene);
  CHECK(plan.steps.size() == 3 && chain.TargetCount() == 2);
  CHECK(chain.Target(0).Width() == 100 && chain.Target(0).Height() == 50);
  CHECK(chain.SceneTarget().Width() == 200 && chain.SceneTarget().Settings().depth);

  // The same size again: the same textures (nothing made per frame).
  const rendering::RenderTarget* first = &chain.Target(0);
  const rendering::RenderTarget* scene_target = &chain.SceneTarget();
  chain.Prepare({200, 100}, scene);
  CHECK(&chain.Target(0) == first && &chain.SceneTarget() == scene_target);

  // A new size: the same textures, resized (materials holding them keep working).
  chain.Prepare({400, 300}, scene);
  CHECK(&chain.Target(0) == first && chain.Target(0).Width() == 200 && chain.Target(0).Height() == 150);
  CHECK(&chain.SceneTarget() == scene_target && chain.SceneTarget().Height() == 300);

  // If the GPU fell back to RGBA8, the targets aren't remade every frame.
  chain.SceneTarget().SetFormat(TextureFormat::RGBA8);
  const rendering::RenderTargetSettings hdr{TextureFormat::RGBA16F, true, true};
  chain.Prepare({400, 300}, hdr);
  const rendering::RenderTarget* hdr_target = &chain.SceneTarget();
  chain.SceneTarget().SetFormat(TextureFormat::RGBA8);  // what the renderer does on such a GPU
  chain.Prepare({400, 300}, hdr);
  CHECK(&chain.SceneTarget() == hdr_target);

  chain.Clear();
  CHECK(chain.Empty());
}

static void TestPostShaders() {
  // `shader_type post` has no vertex(); the engine's post shaders all parse as post shaders.
  CHECK(rendering::SplitShaderSource("shader_type post; void fragment() { FragColor = vec4(1.0); }").target ==
        rendering::Shader::Target::Post);
  CHECK(!rendering::SplitShaderSource("shader_type post; void vertex() {} void fragment() {}").ok);
  for (const char* file :
       {"post/copy.glsl", "post/blur.glsl", "post/bright.glsl", "post/bloom_combine.glsl", "post/threshold.glsl"}) {
    const std::string* text = rendering::FindShaderFile(file);
    CHECK(text != nullptr);
    if (!text) continue;
    const rendering::ShaderSource source = rendering::SplitShaderSource(*text);
    CHECK(source.ok && source.target == rendering::Shader::Target::Post);
  }
  CHECK(rendering::FindShaderFile("custom/post.vert") && rendering::FindShaderFile("custom/post_fragment.glsl"));

  // The blur's weights add up to 1 (the centre once, each side pair twice): it keeps brightness.
  const std::string* blur = rendering::FindShaderFile("post/blur.glsl");
  if (blur) {
    std::vector<float> weights;
    const std::regex weight("\\* (0\\.[0-9]+);");
    for (auto it = std::sregex_iterator(blur->begin(), blur->end(), weight); it != std::sregex_iterator(); ++it) {
      weights.push_back(std::stof((*it)[1].str()));
    }
    CHECK(weights.size() == 3);
    if (weights.size() == 3) CHECK(Near(weights[0] + 2.0f * (weights[1] + weights[2]), 1.0f, 1e-6f));
  }
}

static void TestEffects() {
  // Blur: one pass across and one down per iteration, at its scale.
  auto view = scene::Viewport::Create();
  const scene::Blur blur = scene::AddBlur(*view, {.scale = 0.5f, .iterations = 2});
  const auto& passes = view->Post().Passes();
  CHECK(passes.size() == 4 && blur.passes.size() == 4);
  CHECK(passes[0].scale == 0.5f && passes[0].material == blur.passes[0]);
  const auto* across = std::get_if<glm::vec2>(blur.passes[0]->GetParam("uDirection"));
  const auto* down = std::get_if<glm::vec2>(blur.passes[1]->GetParam("uDirection"));
  CHECK(across && down && across->x == 1.0f && down->y == 1.0f);
  for (const auto& material : blur.passes) {
    CHECK(material->IsValid() && material->GetShader()->GetTarget() == rendering::Shader::Target::Post);
  }
  CHECK(blur.passes[0]->GetShader() == blur.passes[3]->GetShader());  // one shader, shared

  // Bloom on its own: bright, blur x iterations, combine onto the scene picture.
  auto screen = scene::Viewport::Create();
  const scene::Bloom bloom = scene::AddBloom(*screen, {.threshold = 0.7f, .iterations = 1});
  const auto& bloom_passes = screen->Post().Passes();
  CHECK(bloom_passes.size() == 4);
  CHECK(bloom_passes.front().material == bloom.bright && bloom_passes.back().material == bloom.combine);
  CHECK(bloom_passes.back().inputs.size() == 1 && bloom_passes.back().inputs[0].pass == rendering::kScenePicture);
  const auto* threshold = std::get_if<float>(bloom.bright->GetParam("uThreshold"));
  CHECK(threshold && *threshold == 0.7f);

  // Bloom after other passes adds its glow onto their result (the last one gets a name).
  const scene::Bloom after = scene::AddBloom(*view);
  const auto& all = view->Post().Passes();
  CHECK(!all[3].name.empty() && all.back().inputs[0].pass == all[3].name);
  const PostPlan plan = rendering::PlanPostChain(all, kPhone);
  CHECK(plan.errors.empty());
  CheckPlanIsSafe(all, plan);
  CHECK(after.combine && after.bright);

  // Liquid: blur then threshold, the threshold pass last (it writes the viewport's picture).
  auto liquid = scene::Viewport::Create({.format = TextureFormat::RGBA16F, .depth = false});
  scene::AddBlur(*liquid, {.scale = 1.0f});
  const auto threshold_material = scene::AddThreshold(*liquid, {.threshold = 0.4f, .softness = 0.0f});
  const auto& liquid_passes = liquid->Post().Passes();
  CHECK(liquid_passes.size() == 3 && liquid_passes.back().material == threshold_material);
  CHECK(threshold_material->IsValid());
  const auto* level = std::get_if<float>(threshold_material->GetParam("uThreshold"));
  const auto* softness = std::get_if<float>(threshold_material->GetParam("uSoftness"));
  CHECK(level && *level == 0.4f && softness && *softness > 0.0f);  // never 0: smoothstep needs an edge
  CHECK(rendering::PlanPostChain(liquid_passes, kPhone).errors.empty());
}

int main() {
  TestEmptyAndSingle();
  TestPingPong();
  TestSizesAndFormats();
  TestNamedOutputsAreKept();
  TestInputErrors();
  TestChainTargets();
  TestPostShaders();
  TestEffects();
  return TestResult("post");
}
