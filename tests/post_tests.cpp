// Tests for post passes (rendering/post_pass.h: planning which texture each pass reads and writes,
// the chain's targets, the upscale of a render-scaled picture) and the ready-made effects (scene/post_effects.h). CPU
// only: materials, shaders and targets are made without a GPU.
#include <memory>
#include <regex>
#include <set>
#include <string>
#include <vector>

#include "test_helpers.h"
#include "wlw/rendering/post_pass.h"
#include "wlw/rendering/shader.h"
#include "wlw/rendering/shader_library.h"
#include "wlw/scene/post_effects.h"
#include "wlw/scene/viewport.h"

TEST_SUITE_BEGIN("post");

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
      CHECK((step.source >= 0 && holds[static_cast<size_t>(step.source)] == static_cast<int>(i) - 1));
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

TEST_CASE("empty and single") {
  CHECK(rendering::PlanPostChain({}, kPhone).steps.empty());

  // One pass reads the scene picture and writes the destination: no extra textures.
  const std::vector<PostPass> one{Pass(0.25f)};
  const PostPlan plan = rendering::PlanPostChain(one, kPhone);
  CHECK((plan.steps.size() == 1 && plan.targets.empty()));
  CHECK((plan.steps[0].source == kSceneTarget && plan.steps[0].output == kDestination));
  CHECK(plan.steps[0].size == kPhone);  // the last pass is always full size
}

TEST_CASE("ping pong") {
  // A chain of full-size passes needs only two textures, used in turn.
  const std::vector<PostPass> passes{Pass(), Pass(), Pass(), Pass(), Pass()};
  const PostPlan plan = rendering::PlanPostChain(passes, kPhone);
  CHECK(plan.targets.size() == 2);
  CHECK((plan.steps[0].output == 0 && plan.steps[1].output == 1 && plan.steps[2].output == 0 &&
         plan.steps[3].output == 1));
  CheckPlanIsSafe(passes, plan);
}

TEST_CASE("sizes and formats") {
  const std::vector<PostPass> passes{Pass(0.5f), Pass(0.25f), Pass(0.25f, "", {}, TextureFormat::RGBA16F), Pass(0.25f)};
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

TEST_CASE("named outputs are kept") {
  // Pass 3 reads pass 0's output: nothing in between may overwrite it.
  const std::vector<PostPass> passes{Pass(1.0f, "base"), Pass(), Pass(), Pass(1.0f, "", {{"uBase", "base"}})};
  const PostPlan plan = rendering::PlanPostChain(passes, kPhone);
  CHECK(plan.errors.empty());
  const int base = plan.steps[0].output;
  CHECK((plan.steps[1].output != base && plan.steps[2].output != base));
  CHECK((plan.steps[3].inputs.size() == 1 && plan.steps[3].inputs[0].first == "uBase" &&
         plan.steps[3].inputs[0].second == base));
  CHECK(plan.targets.size() == 3);
  CheckPlanIsSafe(passes, plan);

  // Once its last reader is done, the texture is free again.
  const std::vector<PostPass> later{Pass(1.0f, "a"), Pass(1.0f, "", {{"uA", "a"}}), Pass(), Pass(), Pass()};
  const PostPlan reused = rendering::PlanPostChain(later, kPhone);
  CHECK(reused.targets.size() == 2);
  CheckPlanIsSafe(later, reused);
}

TEST_CASE("input errors") {
  // The scene picture by name; an unknown name and a name used twice are reported.
  const std::vector<PostPass> passes{Pass(1.0f, "x", {{"uFirst", rendering::kScenePicture}}), Pass(1.0f, "x"),
                                     Pass(1.0f, "", {{"uX", "x"}, {"uMissing", "nope"}})};
  const PostPlan plan = rendering::PlanPostChain(passes, kPhone);
  CHECK((plan.steps[0].inputs.size() == 1 && plan.steps[0].inputs[0].second == kSceneTarget));
  CHECK(plan.errors.size() == 2);
  // "x" means the latest pass before the reader with that name (pass 1); the unknown input is skipped.
  CHECK((plan.steps[2].inputs.size() == 1 && plan.steps[2].inputs[0].second == plan.steps[1].output));
  CheckPlanIsSafe(passes, plan);

  // Reading a later pass isn't possible (it hasn't run yet).
  const std::vector<PostPass> forward{Pass(1.0f, "", {{"uLater", "later"}}), Pass(1.0f, "later")};
  CHECK(rendering::PlanPostChain(forward, kPhone).errors.size() == 1);
}

TEST_CASE("chain targets") {
  rendering::PostChain chain;
  chain.Add(Pass(0.5f));
  chain.Add(Pass(0.5f));
  chain.Add(Pass());
  const rendering::RenderTargetSettings scene{TextureFormat::RGBA8, true, /*depth=*/true};
  const PostPlan& plan = chain.Internal().Prepare({200, 100}, scene);
  CHECK((plan.steps.size() == 3 && chain.Internal().TargetCount() == 2));
  CHECK((chain.Internal().Target(0).Width() == 100 && chain.Internal().Target(0).Height() == 50));
  CHECK((chain.Internal().SceneTarget().Width() == 200 && chain.Internal().SceneTarget().Settings().depth));

  // The same size again: the same textures (nothing made per frame).
  const rendering::RenderTarget* first = &chain.Internal().Target(0);
  const rendering::RenderTarget* scene_target = &chain.Internal().SceneTarget();
  chain.Internal().Prepare({200, 100}, scene);
  CHECK((&chain.Internal().Target(0) == first && &chain.Internal().SceneTarget() == scene_target));

  // A new size: the same textures, resized (materials holding them keep working).
  chain.Internal().Prepare({400, 300}, scene);
  CHECK((&chain.Internal().Target(0) == first && chain.Internal().Target(0).Width() == 200 &&
         chain.Internal().Target(0).Height() == 150));
  CHECK((&chain.Internal().SceneTarget() == scene_target && chain.Internal().SceneTarget().Height() == 300));

  // If the GPU fell back to RGBA8, the targets aren't remade every frame.
  chain.Internal().SceneTarget().Internal().SetFormat(TextureFormat::RGBA8);
  const rendering::RenderTargetSettings hdr{TextureFormat::RGBA16F, true, true};
  chain.Internal().Prepare({400, 300}, hdr);
  const rendering::RenderTarget* hdr_target = &chain.Internal().SceneTarget();
  chain.Internal().SceneTarget().Internal().SetFormat(TextureFormat::RGBA8);  // what the renderer does on such a GPU
  chain.Internal().Prepare({400, 300}, hdr);
  CHECK(&chain.Internal().SceneTarget() == hdr_target);

  chain.Clear();
  CHECK(chain.Empty());
}

TEST_CASE("render scale upscale") {
  // A scene picture drawn smaller (Viewport::SetRenderScale) without passes: one copy stretches it.
  const PostPlan upscale = rendering::PlanPostChain({}, kPhone, {810, 1800});
  REQUIRE(upscale.steps.size() == 1);
  const rendering::PostStep& copy = upscale.steps[0];
  CHECK((copy.pass == rendering::kUpscalePass && copy.source == kSceneTarget && copy.output == kDestination));
  CHECK((copy.size == kPhone && upscale.targets.empty()));
  CHECK(rendering::PlanPostChain({}, kPhone, kPhone).steps.empty());  // the same size: nothing to do

  // With passes, their sizes stay fractions of the viewport's (an effect looks the same at any
  // render scale) and the last one writes the full-size picture: no extra step.
  const std::vector<PostPass> passes{Pass(0.25f), Pass(0.25f), Pass(1.0f), Pass()};
  const PostPlan full = rendering::PlanPostChain(passes, kPhone);
  const PostPlan scaled = rendering::PlanPostChain(passes, kPhone, {540, 1200});
  REQUIRE(scaled.steps.size() == full.steps.size());
  for (size_t i = 0; i < full.steps.size(); ++i) CHECK(scaled.steps[i].size == full.steps[i].size);
  CHECK(scaled.steps.back().size == kPhone);
  CheckPlanIsSafe(passes, scaled);

  // The chain: the scene picture at the render size, re-planned when it changes.
  rendering::PostChain chain;
  const rendering::RenderTargetSettings scene{TextureFormat::RGBA8, true, /*depth=*/true};
  const PostPlan& plan = chain.Internal().Prepare({200, 100}, scene, {100, 50});
  CHECK((plan.steps.size() == 1 && plan.steps[0].pass == rendering::kUpscalePass));
  CHECK((chain.Internal().SceneTarget().Width() == 100 && chain.Internal().SceneTarget().Height() == 50 &&
         chain.Internal().TargetCount() == 0));
  const rendering::RenderTarget* scene_target = &chain.Internal().SceneTarget();
  // Asked for at the same size (a screen without a depth buffer draws through its picture): still
  // one copy, since the picture must reach the destination.
  const PostPlan& same = chain.Internal().Prepare({200, 100}, scene);
  CHECK((same.steps.size() == 1 && same.steps[0].pass == rendering::kUpscalePass &&
         same.steps[0].size == glm::ivec2(200, 100)));
  CHECK((&chain.Internal().SceneTarget() == scene_target &&
         chain.Internal().SceneTarget().Width() == 200));  // the same texture, resized
}

TEST_CASE("output pass") {
  const rendering::PostPlanOptions output{.output_pass = true};
  // Alone: one step from the scene picture to the destination (it also does a render scale's stretch).
  const PostPlan alone = rendering::PlanPostChain({}, kPhone, {540, 1200}, output);
  REQUIRE(alone.steps.size() == 1);
  CHECK((alone.steps[0].pass == rendering::kOutputPass && alone.steps[0].source == kSceneTarget &&
         alone.steps[0].output == kDestination && alone.steps[0].size == kPhone && alone.targets.empty()));

  // After passes: the last pass now writes a target, the output pass reads it and writes the destination.
  const std::vector<PostPass> passes{Pass(0.5f), Pass(), Pass(1.0f, "", {{"uFirst", rendering::kScenePicture}})};
  const PostPlan plan = rendering::PlanPostChain(passes, kPhone, {0, 0}, output);
  REQUIRE(plan.steps.size() == 4);
  const rendering::PostStep& last = plan.steps[3];
  CHECK((last.pass == rendering::kOutputPass && last.output == kDestination && last.size == kPhone));
  CHECK((plan.steps[2].output >= 0 && last.source == plan.steps[2].output));
  CHECK(plan.steps[2].size == kPhone);  // a full-size pass keeps its size
  // The rules of the plan still hold (the checker expects one step per pass: without the output step).
  PostPlan passes_only = plan;
  passes_only.steps.pop_back();
  for (size_t i = 0; i < passes_only.steps.size(); ++i) CHECK(passes_only.steps[i].output >= 0);
  CHECK(plan.steps[1].output != plan.steps[2].output);
}

TEST_CASE("pass formats follow the scene picture") {
  // Passes without a format of their own write the scene picture's: an HDR viewport stays HDR.
  const std::vector<PostPass> passes{PostPass{.scale = 0.5f}, Pass(0.5f, "", {}, TextureFormat::RGBA8), PostPass{}};
  const PostPlan hdr = rendering::PlanPostChain(passes, kPhone, {0, 0}, {.scene_format = TextureFormat::RGBA16F});
  CHECK(hdr.targets[static_cast<size_t>(hdr.steps[0].output)].format == TextureFormat::RGBA16F);
  CHECK(hdr.targets[static_cast<size_t>(hdr.steps[1].output)].format == TextureFormat::RGBA8);  // its own
  const PostPlan ldr = rendering::PlanPostChain(passes, kPhone);
  CHECK(ldr.targets[static_cast<size_t>(ldr.steps[0].output)].format == TextureFormat::RGBA8);

  // The ready-made effects follow it too, unless given a format.
  auto view = scene::Viewport::Create();
  scene::AddBlur(*view);
  scene::AddBlur(*view, {.format = TextureFormat::RGBA16F});
  CHECK((!view->Post().Passes()[0].format && view->Post().Passes()[2].format == TextureFormat::RGBA16F));
}

TEST_CASE("settings re-plan the chain") {
  rendering::PostChain chain;
  CHECK((!chain.HasOutputPass() && !chain.HasWork()));
  const rendering::RenderTargetSettings scene{TextureFormat::RGBA16F, true, true};
  // Nothing asked for at the same size: one copy (see "render scale upscale").
  CHECK(chain.Internal().Prepare({200, 100}, scene).steps[0].pass == rendering::kUpscalePass);

  // Turning tone mapping on adds the output pass at the next Prepare; turning it off removes it.
  chain.Settings().tone_map.mapper = rendering::ToneMapper::Shoulder;
  CHECK((chain.HasOutputPass() && chain.HasWork()));
  const PostPlan& on = chain.Internal().Prepare({200, 100}, scene);
  CHECK((on.steps.size() == 1 && on.steps[0].pass == rendering::kOutputPass));
  chain.Settings().tone_map.mapper = rendering::ToneMapper::Clamp;
  CHECK(chain.Internal().Prepare({200, 100}, scene).steps[0].pass == rendering::kUpscalePass);

  // Each setting away from its "do nothing" value needs it.
  CHECK(!rendering::PostSettings{}.NeedsOutputPass());
  CHECK(rendering::PostSettings{.tone_map = {.exposure = 1.5f}}.NeedsOutputPass());
  CHECK(rendering::PostSettings{.tone_map = {.dither = true}}.NeedsOutputPass());

  // A pass that inherits the format follows the scene picture when it changes.
  chain.Add(PostPass{.scale = 0.5f});
  chain.Add(PostPass{});
  chain.Internal().Prepare({200, 100}, {TextureFormat::RGBA8, true, true});
  CHECK(chain.Internal().Target(0).Settings().format == TextureFormat::RGBA8);
  chain.Internal().Prepare({200, 100}, scene);
  CHECK(chain.Internal().Target(0).Settings().format == TextureFormat::RGBA16F);
}

TEST_CASE("viewport format") {
  // The screen's picture format: its own, or the override (the r.hdr cvar) while set.
  auto view = scene::Viewport::Create();
  CHECK(view->EffectiveFormat() == TextureFormat::RGBA8);
  view->SetFormat(TextureFormat::RGBA16F);
  CHECK((view->EffectiveFormat() == TextureFormat::RGBA16F && view->Settings().format == TextureFormat::RGBA16F));
  view->SetFormatOverride(TextureFormat::RGBA8);
  CHECK(view->EffectiveFormat() == TextureFormat::RGBA8);
  // An offscreen viewport's texture follows (the GPU storage is remade before its next draw).
  CHECK(view->GetTexture()->Settings().format == TextureFormat::RGBA8);
  view->SetFormatOverride(std::nullopt);
  CHECK((view->EffectiveFormat() == TextureFormat::RGBA16F &&
         view->GetTexture()->Settings().format == TextureFormat::RGBA16F));
}

TEST_CASE("post shaders") {
  // `shader_type post` has no vertex(); the engine's post shaders all parse as post shaders.
  CHECK(rendering::SplitShaderSource("shader_type post; void fragment() { FragColor = vec4(1.0); }").target ==
        rendering::Shader::Target::Post);
  CHECK(!rendering::SplitShaderSource("shader_type post; void vertex() {} void fragment() {}").ok);
  for (const char* file : {"post/copy.glsl", "post/blur.glsl", "post/bloom_down.glsl", "post/bloom_up.glsl",
                           "post/threshold.glsl", "post/output.glsl"}) {
    const std::string* text = rendering::FindShaderFile(file);
    CHECK(text != nullptr);
    if (!text) continue;
    const rendering::ShaderSource source = rendering::SplitShaderSource(*text);
    CHECK((source.ok && source.target == rendering::Shader::Target::Post));
  }
  CHECK((rendering::FindShaderFile("custom/post.vert") && rendering::FindShaderFile("custom/post_fragment.glsl")));

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

TEST_CASE("effects") {
  // Blur: one pass across and one down per iteration, at its scale.
  auto view = scene::Viewport::Create();
  const scene::Blur blur = scene::AddBlur(*view, {.scale = 0.5f, .iterations = 2});
  const auto& passes = view->Post().Passes();
  CHECK((passes.size() == 4 && blur.passes.size() == 4));
  CHECK((passes[0].scale == 0.5f && passes[0].material == blur.passes[0]));
  const auto* across = std::get_if<glm::vec2>(blur.passes[0]->GetParam("uDirection"));
  const auto* down = std::get_if<glm::vec2>(blur.passes[1]->GetParam("uDirection"));
  CHECK((across && down && across->x == 1.0f && down->y == 1.0f));
  for (const auto& material : blur.passes) {
    CHECK((material->IsValid() && material->GetShader()->GetTarget() == rendering::Shader::Target::Post));
  }
  CHECK(blur.passes[0]->GetShader() == blur.passes[3]->GetShader());  // one shader, shared

  const PostPlan plan = rendering::PlanPostChain(passes, kPhone);
  CHECK(plan.errors.empty());
  CheckPlanIsSafe(passes, plan);

  // Liquid: blur then threshold, the threshold pass last (it writes the viewport's picture).
  auto liquid = scene::Viewport::Create({.format = TextureFormat::RGBA16F, .depth = false});
  scene::AddBlur(*liquid, {.scale = 1.0f});
  const auto threshold_material = scene::AddThreshold(*liquid, {.threshold = 0.4f, .softness = 0.0f});
  const auto& liquid_passes = liquid->Post().Passes();
  CHECK((liquid_passes.size() == 3 && liquid_passes.back().material == threshold_material));
  CHECK(threshold_material->IsValid());
  const auto* level = std::get_if<float>(threshold_material->GetParam("uThreshold"));
  const auto* softness = std::get_if<float>(threshold_material->GetParam("uSoftness"));
  CHECK((level && *level == 0.4f && softness && *softness > 0.0f));  // never 0: smoothstep needs an edge
  CHECK(rendering::PlanPostChain(liquid_passes, kPhone).errors.empty());
}

TEST_CASE("scale while running") {
  // A quality level changes an effect's resolution: its passes only (found by their materials,
  // whatever was added around them), and the targets follow at the next Prepare.
  auto view = scene::Viewport::Create();
  view->AddPostPass(Pass(1.0f, "first"));
  const scene::Blur blur = scene::AddBlur(*view, {.scale = 0.25f, .iterations = 1});
  view->AddPostPass(Pass(1.0f, "last"));
  const rendering::RenderTargetSettings scene{};
  rendering::PostChain& chain = view->Post();
  chain.Internal().Prepare({400, 200}, scene);
  const int targets = chain.Internal().TargetCount();
  auto has_width = [&chain](int width) {
    for (int i = 0; i < chain.Internal().TargetCount(); ++i) {
      if (chain.Internal().Target(i).Width() == width) return true;
    }
    return false;
  };
  CHECK(has_width(100));  // a quarter of 400

  blur.SetScale(*view, 0.125f);
  const auto& passes = chain.Passes();
  CHECK(passes[0].scale == 1.0f);  // not the blur's
  CHECK((passes[1].scale == 0.125f && passes[2].scale == 0.125f));
  CHECK(passes[3].scale == 1.0f);
  const PostPlan& plan = chain.Internal().Prepare({400, 200}, scene);
  CHECK(plan.steps[1].size == glm::ivec2(50, 25));
  CHECK(chain.Internal().TargetCount() == targets);
  CHECK((has_width(50) && !has_width(100)));
}

namespace {

// Plays a plan with bloom after `passes` game passes and checks every read finds what it means:
// down level 0 reads the picture (the last pass's output, or the scene), each next level the one
// before; each up level the step before it (the smallest down level, or the up level below) as
// uSource and its own down level as uLevel; the output pass the picture and the glow (uBloom).
void CheckBloomPlan(const PostPlan& plan, int passes) {
  const int levels = plan.bloom_levels;
  REQUIRE(levels >= 1);
  REQUIRE(plan.steps.size() == static_cast<size_t>(passes + 2 * levels));  // + levels - 1 up, + output
  std::vector<int> holds(plan.targets.size(), -1);                         // per target: which step's output
  auto finds = [&holds](int texture, int step) {
    return step < 0 ? texture == kSceneTarget : texture >= 0 && holds[static_cast<size_t>(texture)] == step;
  };
  const int picture = passes - 1;  // -1: the scene picture
  for (int s = 0; s < static_cast<int>(plan.steps.size()); ++s) {
    const rendering::PostStep& step = plan.steps[static_cast<size_t>(s)];
    auto input = [&step](const std::string& uniform) {
      for (const auto& [name, texture] : step.inputs) {
        if (name == uniform) return texture;
      }
      return -100;
    };
    if (s < passes) {
      CHECK(step.pass == s);
    } else if (s < passes + levels) {
      const int k = s - passes;
      CHECK((step.pass == rendering::kBloomDownPass && step.level == k));
      CHECK(finds(step.source, k == 0 ? picture : s - 1));
    } else if (s < passes + 2 * levels - 1) {
      const int k = levels - 2 - (s - passes - levels);
      CHECK((step.pass == rendering::kBloomUpPass && step.level == k));
      CHECK(finds(step.source, s - 1));
      CHECK(finds(input("uLevel"), passes + k));
      CHECK(step.size == plan.steps[static_cast<size_t>(passes + k)].size);  // the same size as its down level
    } else {
      CHECK(step.pass == rendering::kOutputPass);
      CHECK(finds(step.source, picture));
      CHECK(finds(input("uBloom"), s - 1));
      CHECK(step.output == kDestination);
    }
    CHECK(step.output != step.source);
    for (const auto& [_, texture] : step.inputs) CHECK(texture != step.output);
    if (step.output >= 0) {
      CHECK(plan.targets[static_cast<size_t>(step.output)].size == step.size);
      holds[static_cast<size_t>(step.output)] = s;
    }
  }
}

const rendering::PostPlanOptions kBloom{
    .scene_format = TextureFormat::RGBA16F, .output_pass = true, .bloom = {.enabled = true}};

}  // namespace

TEST_CASE("bloom levels") {
  // Pixel 4 (1080 x 2280) from half size: halved (rounded up) down to 17 x 36, the next would be 9 x 18.
  const std::vector<glm::ivec2> pixel4 = rendering::BloomLevelSizes({1080, 2280}, 0.5f, 6);
  const std::vector<glm::ivec2> expected{{540, 1140}, {270, 570}, {135, 285}, {68, 143}, {34, 72}, {17, 36}};
  CHECK(pixel4 == expected);
  CHECK(rendering::BloomLevelSizes({1080, 2280}, 0.5f, 7).size() == 6);  // stopped at 16 px
  CHECK(rendering::BloomLevelSizes({1080, 2280}, 0.5f, 3).size() == 3);  // max_levels
  CHECK(rendering::BloomLevelSizes({1080, 2280}, 0.25f, 6).front() == glm::ivec2(270, 570));
  // Never more than kMaxBloomLevels, never fewer than 1 (even on a tiny viewport or with 0 asked for).
  CHECK(rendering::BloomLevelSizes({8000, 8000}, 1.0f, 20).size() == rendering::kMaxBloomLevels);
  CHECK(rendering::BloomLevelSizes({40, 40}, 0.5f, 6) == std::vector<glm::ivec2>{{20, 20}});
  CHECK(rendering::BloomLevelSizes({1080, 2280}, 0.5f, 0).size() == 1);
}

TEST_CASE("bloom plan") {
  // Alone: 6 levels down, 5 up, the output pass. HDR targets (the scene picture's format).
  const PostPlan alone = rendering::PlanPostChain({}, {1080, 2280}, {0, 0}, kBloom);
  CHECK((alone.bloom_levels == 6 && alone.errors.empty()));
  CheckBloomPlan(alone, 0);
  for (const rendering::PostTargetSpec& target : alone.targets) CHECK(target.format == TextureFormat::RGBA16F);
  CHECK(alone.steps.back().size == glm::ivec2(1080, 2280));

  // After game passes: the bloom reads the last one's output, which stays until the output pass.
  const std::vector<PostPass> passes{PostPass{.scale = 0.5f}, PostPass{}};
  const PostPlan after = rendering::PlanPostChain(passes, kPhone, {0, 0}, kBloom);
  CheckBloomPlan(after, 2);
  // Targets are shared: level 0 (half size) reuses the first pass's (half size, free once pass 1 read it).
  CHECK(after.steps[2].output == after.steps[0].output);
  CHECK(after.targets.size() < after.steps.size() - 1);

  // One level only: no up steps, the output pass reads level 0 as the glow.
  const PostPlan one =
      rendering::PlanPostChain({}, kPhone, {0, 0}, {.output_pass = true, .bloom = {.enabled = true, .max_levels = 1}});
  CHECK(one.bloom_levels == 1);
  CheckBloomPlan(one, 0);

  // A render scale doesn't change the levels (they follow the viewport's size; level 0 stretches).
  const PostPlan scaled = rendering::PlanPostChain({}, kPhone, {540, 1200}, kBloom);
  CHECK(scaled.steps[0].size == glm::ivec2(540, 1200));
  CHECK(scaled.steps.size() == rendering::PlanPostChain({}, kPhone, {0, 0}, kBloom).steps.size());

  // Off, or without the output pass (it adds the glow): no bloom steps.
  const PostPlan off = rendering::PlanPostChain({}, kPhone, {0, 0}, {.output_pass = true});
  CHECK((off.bloom_levels == 0 && off.steps.size() == 1));
  CHECK(rendering::PlanPostChain(passes, kPhone, {0, 0}, {.bloom = {.enabled = true}}).bloom_levels == 0);
}

TEST_CASE("bloom settings re-plan the chain") {
  rendering::PostChain chain;
  const rendering::RenderTargetSettings scene{TextureFormat::RGBA16F, true, true};
  CHECK(!chain.HasOutputPass());
  chain.Settings().bloom.enabled = true;  // bloom alone needs the output pass (it adds the glow there)
  CHECK((chain.HasOutputPass() && rendering::PostSettings{.bloom = {.enabled = true}}.NeedsOutputPass()));
  const PostPlan& on = chain.Internal().Prepare({400, 200}, scene);
  CHECK((on.bloom_levels == 3 && on.steps[0].size == glm::ivec2(200, 100)));  // 200x100, 100x50, 50x25
  const int targets = chain.Internal().TargetCount();

  // Scale and levels shape the plan: re-planned at the next Prepare.
  chain.Settings().bloom.scale = 0.25f;
  CHECK(chain.Internal().Prepare({400, 200}, scene).steps[0].size == glm::ivec2(100, 50));
  chain.Settings().bloom.max_levels = 1;
  CHECK(chain.Internal().Prepare({400, 200}, scene).bloom_levels == 1);
  // The others are shader params: the same plan, the same textures.
  chain.Settings().bloom.max_levels = 6;
  chain.Settings().bloom.scale = 0.5f;
  chain.Internal().Prepare({400, 200}, scene);
  CHECK(chain.Internal().TargetCount() == targets);
  chain.Settings().bloom.threshold = 2.0f;
  chain.Settings().bloom.intensity = 0.1f;
  CHECK(chain.Internal().Prepare({400, 200}, scene).bloom_levels == 3);

  // The override (cvar r.bloom) wins over the settings either way.
  chain.SetBloomOverride(false);
  CHECK((!chain.HasOutputPass() && chain.Internal().Prepare({400, 200}, scene).bloom_levels == 0));
  CHECK(chain.Settings().bloom.enabled);  // the game's setting is untouched
  chain.Settings().bloom.enabled = false;
  chain.SetBloomOverride(true);
  CHECK(chain.Internal().Prepare({400, 200}, scene).bloom_levels == 3);
  chain.SetBloomOverride(std::nullopt);
  CHECK(chain.Internal().Prepare({400, 200}, scene).bloom_levels == 0);
}

TEST_SUITE_END();
