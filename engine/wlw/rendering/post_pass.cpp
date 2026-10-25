#include "wlw/rendering/post_pass.h"

#include <algorithm>
#include <cmath>
#include <unordered_map>

#include "wlw/core/log.h"

namespace wlw::rendering {

namespace {

glm::ivec2 ScaledSize(glm::ivec2 pixels, float scale) {
  scale = std::max(scale, 0.0f);
  return {std::max(1, static_cast<int>(std::lround(static_cast<float>(pixels.x) * scale))),
          std::max(1, static_cast<int>(std::lround(static_cast<float>(pixels.y) * scale)))};
}

}  // namespace

PostPlan PlanPostChain(std::span<const PostPass> passes, glm::ivec2 pixels, glm::ivec2 scene_pixels,
                       const PostPlanOptions& options) {
  PostPlan plan;
  pixels = glm::max(pixels, glm::ivec2(1));
  if (scene_pixels.x <= 0 || scene_pixels.y <= 0) scene_pixels = pixels;
  const int count = static_cast<int>(passes.size());
  if (count == 0 && !options.output_pass) {
    // A scene picture drawn at another size (render scale): a copy stretches it to the destination.
    if (scene_pixels != pixels) {
      plan.steps.push_back({.pass = kUpscalePass, .source = kSceneTarget, .output = kDestination, .size = pixels});
    }
    return plan;
  }

  // Which earlier pass each input means (-1: the scene picture), resolved in order: a name means
  // the latest pass before the reader that has it.
  std::vector<std::vector<std::pair<std::string, int>>> reads(static_cast<size_t>(count));  // uniform -> pass
  std::unordered_map<std::string, int> named;
  for (int i = 0; i < count; ++i) {
    const PostPass& pass = passes[static_cast<size_t>(i)];
    for (const PostInput& input : pass.inputs) {
      if (input.pass == kScenePicture) {
        reads[static_cast<size_t>(i)].emplace_back(input.uniform, -1);
      } else if (auto it = named.find(input.pass); it != named.end()) {
        reads[static_cast<size_t>(i)].emplace_back(input.uniform, it->second);
      } else {
        plan.errors.push_back("post pass " + std::to_string(i) + ": no earlier pass named '" + input.pass + "'");
      }
    }
    if (!pass.name.empty()) {
      if (pass.name == kScenePicture) {
        plan.errors.push_back("post pass " + std::to_string(i) + ": '" + pass.name + "' is the scene picture's name");
      } else if (!named.emplace(pass.name, i).second) {
        plan.errors.push_back("post pass " + std::to_string(i) + ": name '" + pass.name +
                              "' is used twice (later inputs read this one)");
        named[pass.name] = i;
      }
    }
  }

  // The steps first, each reading earlier steps by their index (kSceneTarget: the scene picture);
  // which target each one writes is chosen after, once it's known which step reads it last.
  // The game's passes are steps 0..count-1 (their reads are pass indices, so step indices too).
  std::vector<PostStep> steps;
  std::vector<TextureFormat> formats;  // per step: its output's (if it writes a target)
  for (int i = 0; i < count; ++i) {
    const PostPass& pass = passes[static_cast<size_t>(i)];
    steps.push_back({.pass = i,
                     .source = i == 0 ? kSceneTarget : i - 1,
                     .size = ScaledSize(pixels, pass.scale),
                     .inputs = reads[static_cast<size_t>(i)]});
    formats.push_back(pass.format.value_or(options.scene_format));
  }
  // What the bloom and the output pass read: the last pass's output, or the scene picture.
  const int picture = count > 0 ? count - 1 : kSceneTarget;
  int glow = -1;  // the step that writes the finished glow
  if (options.output_pass && options.bloom.enabled) {
    // Down: each level a half of the one before. Up: level k = its own down level + the level below
    // it built up (the smallest level is its own "built up" version).
    const std::vector<glm::ivec2> sizes = BloomLevelSizes(pixels, options.bloom.scale, options.bloom.max_levels);
    const int levels = static_cast<int>(sizes.size());
    plan.bloom_levels = levels;
    std::vector<int> down(static_cast<size_t>(levels));
    for (int k = 0; k < levels; ++k) {
      down[static_cast<size_t>(k)] = static_cast<int>(steps.size());
      steps.push_back({.pass = kBloomDownPass,
                       .source = k == 0 ? picture : down[static_cast<size_t>(k - 1)],
                       .size = sizes[static_cast<size_t>(k)],
                       .level = k});
      formats.push_back(options.scene_format);
    }
    glow = down.back();
    for (int k = levels - 2; k >= 0; --k) {
      const int up = static_cast<int>(steps.size());
      steps.push_back({.pass = kBloomUpPass,
                       .source = glow,
                       .size = sizes[static_cast<size_t>(k)],
                       .inputs = {{"uLevel", down[static_cast<size_t>(k)]}},
                       .level = k});
      formats.push_back(options.scene_format);
      glow = up;
    }
  }
  if (options.output_pass) {
    PostStep output{.pass = kOutputPass, .source = picture, .size = pixels};
    if (glow >= 0) output.inputs.emplace_back("uBloom", glow);
    steps.push_back(std::move(output));
    formats.push_back(options.scene_format);
  }

  // The last step that reads each step's output (uSource or an input).
  std::vector<int> last_read(steps.size(), -1);
  for (int s = 0; s < static_cast<int>(steps.size()); ++s) {
    const PostStep& step = steps[static_cast<size_t>(s)];
    if (step.source >= 0) last_read[static_cast<size_t>(step.source)] = s;
    for (const auto& [_, read] : step.inputs) {
      if (read >= 0) last_read[static_cast<size_t>(read)] = std::max(last_read[static_cast<size_t>(read)], s);
    }
  }

  // The last step writes the destination, at full size. Each step before it writes into a target of
  // its size and format that nobody reads at or after this step (busy_until < s), else a new one.
  const int last = static_cast<int>(steps.size()) - 1;
  std::vector<int> busy_until;  // per target: the last step that reads what it holds
  std::vector<int> output_of(steps.size(), kDestination);
  auto target_of = [&output_of](int read) { return read < 0 ? kSceneTarget : output_of[static_cast<size_t>(read)]; };
  for (int s = 0; s <= last; ++s) {
    PostStep& step = steps[static_cast<size_t>(s)];
    step.source = target_of(step.source);
    for (auto& [_, read] : step.inputs) read = target_of(read);
    if (s == last) {
      step.output = kDestination;
      step.size = pixels;
    } else {
      const PostTargetSpec spec{step.size, formats[static_cast<size_t>(s)]};
      int chosen = -1;
      for (int t = 0; t < static_cast<int>(plan.targets.size()); ++t) {
        const PostTargetSpec& existing = plan.targets[static_cast<size_t>(t)];
        if (busy_until[static_cast<size_t>(t)] < s && existing.size == spec.size && existing.format == spec.format) {
          chosen = t;
          break;
        }
      }
      if (chosen < 0) {
        chosen = static_cast<int>(plan.targets.size());
        plan.targets.push_back(spec);
        busy_until.push_back(-1);
      }
      busy_until[static_cast<size_t>(chosen)] = last_read[static_cast<size_t>(s)];
      step.output = chosen;
    }
    output_of[static_cast<size_t>(s)] = step.output;
  }
  plan.steps = std::move(steps);
  return plan;
}

std::vector<glm::ivec2> BloomLevelSizes(glm::ivec2 pixels, float scale, int max_levels) {
  std::vector<glm::ivec2> sizes{ScaledSize(glm::max(pixels, glm::ivec2(1)), scale)};
  const int count = std::clamp(max_levels, 1, kMaxBloomLevels);
  while (static_cast<int>(sizes.size()) < count) {
    const glm::ivec2 next = (sizes.back() + 1) / 2;
    if (std::min(next.x, next.y) < 16) break;
    sizes.push_back(next);
  }
  return sizes;
}

const PostPlan& PostChain::Prepare(glm::ivec2 pixels, const RenderTargetSettings& scene, glm::ivec2 scene_pixels) {
  if (scene_pixels.x <= 0 || scene_pixels.y <= 0) scene_pixels = pixels;
  const PostSettings settings = EffectiveSettings();
  PostPlanOptions options{.scene_format = scene.format, .output_pass = settings.NeedsOutputPass()};
  // Only the bloom fields that shape the plan (the others are shader params, read every frame).
  if (settings.bloom.enabled) {
    options.bloom.enabled = true;
    options.bloom.scale = settings.bloom.scale;
    options.bloom.max_levels = settings.bloom.max_levels;
  }
  const BloomSettings& planned_bloom = planned_options_.bloom;
  const bool bloom_changed = options.bloom.enabled != planned_bloom.enabled ||
                             options.bloom.scale != planned_bloom.scale ||
                             options.bloom.max_levels != planned_bloom.max_levels;
  if (!planned_ || pixels != planned_pixels_ || scene_pixels != planned_scene_pixels_ ||
      options.scene_format != planned_options_.scene_format || options.output_pass != planned_options_.output_pass ||
      bloom_changed) {
    plan_ = PlanPostChain(passes_, pixels, scene_pixels, options);
    planned_options_ = options;
    for (const std::string& error : plan_.errors) WLW_LOG(LogRender, Warning, "%s", error.c_str());
    planned_ = true;
    planned_pixels_ = pixels;
    planned_scene_pixels_ = scene_pixels;
    // The targets, at the plan's sizes: kept and reused from frame to frame (a resize only
    // changes their size).
    // (Compared with the formats asked for: the GPU may have fallen back to RGBA8.)
    targets_.resize(plan_.targets.size());
    target_formats_.resize(plan_.targets.size());
    for (size_t t = 0; t < targets_.size(); ++t) {
      const PostTargetSpec& spec = plan_.targets[t];
      if (!targets_[t] || target_formats_[t] != spec.format) {
        targets_[t] = RenderTarget::Create({spec.format, /*smooth=*/true, /*depth=*/false});
        target_formats_[t] = spec.format;
      }
      targets_[t]->SetSize(spec.size.x, spec.size.y);
    }
  }
  if (!scene_target_ || scene_settings_.depth != scene.depth || scene_settings_.smooth != scene.smooth ||
      scene_settings_.format != scene.format) {
    scene_target_ = RenderTarget::Create(scene);
    scene_settings_ = scene;
  }
  scene_target_->SetSize(scene_pixels.x, scene_pixels.y);
  // Asked for without passes at the same size (a screen without depth): the scene picture still has
  // to reach the destination, so one copy.
  if (plan_.steps.empty()) {
    plan_.steps.push_back({.pass = kUpscalePass, .source = kSceneTarget, .output = kDestination, .size = pixels});
  }
  return plan_;
}

}  // namespace wlw::rendering
