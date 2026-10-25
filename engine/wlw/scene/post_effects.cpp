#include "wlw/scene/post_effects.h"

#include <algorithm>
#include <string>
#include <unordered_map>

#include "wlw/rendering/shader.h"
#include "wlw/rendering/shader_library.h"

namespace wlw::scene {

namespace {

using rendering::PostPass;
using rendering::ShaderMaterial;

// One of the engine's post shaders (shaders/post/*), made once and shared by every material that
// uses it while any is alive.
std::shared_ptr<rendering::Shader> EngineShader(const std::string& file) {
  static std::unordered_map<std::string, std::weak_ptr<rendering::Shader>> made;
  if (auto shader = made[file].lock()) return shader;
  const std::string* text = rendering::FindShaderFile(file);
  std::shared_ptr<rendering::Shader> shader = rendering::Shader::Create(text ? *text : std::string(), file);
  made[file] = shader;
  return shader;
}

std::shared_ptr<ShaderMaterial> Material(const std::string& file) { return ShaderMaterial::Create(EngineShader(file)); }

// The passes of `viewport` drawn with `material` (an effect's passes are found by their materials,
// so passes added or removed around them don't matter).
void SetScaleOf(Viewport& viewport, const ShaderMaterial* material, float scale) {
  rendering::PostChain& chain = viewport.Post();
  for (size_t i = 0; i < chain.Passes().size(); ++i) {
    if (chain.Passes()[i].material.get() == material) chain.SetScale(i, scale);
  }
}

// The blur's passes, appended to `viewport`.
void AppendBlur(Viewport& viewport, float scale, float spread, int iterations,
                std::optional<rendering::TextureFormat> format,
                std::vector<std::shared_ptr<ShaderMaterial>>& materials) {
  for (int i = 0; i < std::max(iterations, 1); ++i) {
    for (const glm::vec2 direction : {glm::vec2(1.0f, 0.0f), glm::vec2(0.0f, 1.0f)}) {
      auto material = Material("post/blur.glsl");
      material->SetParam("uDirection", direction);
      material->SetParam("uSpread", spread);
      materials.push_back(material);
      viewport.AddPostPass({.material = material, .scale = scale, .format = format});
    }
  }
}

}  // namespace

Blur AddBlur(Viewport& viewport, const BlurSettings& settings) {
  Blur blur;
  AppendBlur(viewport, settings.scale, settings.spread, settings.iterations, settings.format, blur.passes);
  return blur;
}

void Blur::SetScale(Viewport& viewport, float scale) const {
  for (const auto& material : passes) SetScaleOf(viewport, material.get(), scale);
}

std::shared_ptr<ShaderMaterial> AddThreshold(Viewport& viewport, const ThresholdSettings& settings) {
  auto material = Material("post/threshold.glsl");
  material->SetParam("uThreshold", settings.threshold);
  material->SetParam("uSoftness", std::max(settings.softness, 1e-4f));
  viewport.AddPostPass({.material = material});
  return material;
}

}  // namespace wlw::scene
