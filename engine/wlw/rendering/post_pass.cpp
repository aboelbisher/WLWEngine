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

PostPlan PlanPostChain(std::span<const PostPass> passes, glm::ivec2 pixels, glm::ivec2 scene_pixels) {
  PostPlan plan;
  pixels = glm::max(pixels, glm::ivec2(1));
  if (scene_pixels.x <= 0 || scene_pixels.y <= 0) scene_pixels = pixels;
  const int count = static_cast<int>(passes.size());
  if (count == 0) {
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

  // The last step that reads each pass's output: the next pass (uSource), or a later input.
  std::vector<int> last_read(static_cast<size_t>(count), -1);
  for (int i = 0; i < count; ++i) {
    if (i + 1 < count) last_read[static_cast<size_t>(i)] = i + 1;
    for (const auto& [_, pass] : reads[static_cast<size_t>(i)]) {
      if (pass >= 0) last_read[static_cast<size_t>(pass)] = std::max(last_read[static_cast<size_t>(pass)], i);
    }
  }

  // Each pass before the last writes into a target of its size and format that nobody reads at or
  // after this step (busy_until < i), else a new one.
  std::vector<int> busy_until;  // per target: the last step that reads what it holds
  std::vector<int> output_of(static_cast<size_t>(count), kDestination);
  for (int i = 0; i < count; ++i) {
    const PostPass& pass = passes[static_cast<size_t>(i)];
    PostStep step;
    step.pass = i;
    step.source = i == 0 ? kSceneTarget : output_of[static_cast<size_t>(i - 1)];
    for (const auto& [uniform, read] : reads[static_cast<size_t>(i)]) {
      step.inputs.emplace_back(uniform, read < 0 ? kSceneTarget : output_of[static_cast<size_t>(read)]);
    }
    if (i == count - 1) {
      step.output = kDestination;
      step.size = pixels;
    } else {
      const PostTargetSpec spec{ScaledSize(pixels, pass.scale), pass.format};
      int chosen = -1;
      for (int t = 0; t < static_cast<int>(plan.targets.size()); ++t) {
        const PostTargetSpec& existing = plan.targets[static_cast<size_t>(t)];
        if (busy_until[static_cast<size_t>(t)] < i && existing.size == spec.size && existing.format == spec.format) {
          chosen = t;
          break;
        }
      }
      if (chosen < 0) {
        chosen = static_cast<int>(plan.targets.size());
        plan.targets.push_back(spec);
        busy_until.push_back(-1);
      }
      busy_until[static_cast<size_t>(chosen)] = last_read[static_cast<size_t>(i)];
      step.output = chosen;
      step.size = spec.size;
    }
    output_of[static_cast<size_t>(i)] = step.output;
    plan.steps.push_back(std::move(step));
  }
  return plan;
}

const PostPlan& PostChain::Prepare(glm::ivec2 pixels, const RenderTargetSettings& scene, glm::ivec2 scene_pixels) {
  if (scene_pixels.x <= 0 || scene_pixels.y <= 0) scene_pixels = pixels;
  if (!planned_ || pixels != planned_pixels_ || scene_pixels != planned_scene_pixels_) {
    plan_ = PlanPostChain(passes_, pixels, scene_pixels);
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
  return plan_;
}

}  // namespace wlw::rendering
