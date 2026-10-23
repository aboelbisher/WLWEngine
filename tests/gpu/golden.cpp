#include "golden.h"

#include <filesystem>
#include <utility>

#include "support/image_compare.h"
#include "wlw/platform/platform.h"
#include "wlw/rendering/render_target.h"
#include "wlw/scene/window.h"
#include "wlw/wlw.h"

namespace wlw::gpu_tests {
namespace {

bool g_update = false;
std::string g_dump_directory;

// One engine and hidden window per process (ctest runs every case in its own process).
struct Gpu {
  std::unique_ptr<WLWEngine> engine;
  std::shared_ptr<scene::Window> window;
  bool ok = false;
};

Gpu& GetGpu() {
  static Gpu gpu = [] {
    Gpu made;
    made.engine = WLWEngine::Create();
    made.window = scene::Window::Create({kSize, kSize}, "wlw_gpu_tests", {.visible = false});
    made.ok = made.engine->Start(made.window);
    if (made.ok) MESSAGE("GPU: " << made.engine->GetRenderingDriver()->GpuName());
    return made;
  }();
  return gpu;
}

// llvmpipe made the references: strict there; looser on real GPUs.
testing::CompareSettings SettingsFor(const std::string& gpu_name) {
  if (gpu_name.find("llvmpipe") != std::string::npos) return {.channel_tolerance = 2, .max_bad_fraction = 0.001};
  return {.channel_tolerance = 16, .max_bad_fraction = 0.005};  // RTX 5080: largest difference 8
}

}  // namespace

void SetUpdateReferences(bool update) { g_update = update; }

bool UseGpuContext() {
  Gpu& gpu = GetGpu();
  if (!gpu.ok) return false;
  gpu.window->MakeContextCurrent();
  return platform::LoadGLFunctions();
}

void SetShaderDumpDirectory(std::string directory) { g_dump_directory = std::move(directory); }
const std::string& ShaderDumpDirectory() { return g_dump_directory; }

std::shared_ptr<scene::Viewport> MakeViewport() {
  auto viewport = scene::Viewport::Create({.name = "golden", .clear = Color{0.08f, 0.09f, 0.14f, 1.0f}});
  viewport->SetSize(kSize, kSize);
  return viewport;
}

testing::Image Render(const std::shared_ptr<scene::Viewport>& viewport) {
  Gpu& gpu = GetGpu();
  REQUIRE_MESSAGE(gpu.ok, "no GL 3.3 context (on Linux CI: run under xvfb-run with Mesa)");
  rendering::RenderingDriver& driver = *gpu.engine->GetRenderingDriver();

  const int id = gpu.window->AddViewport(viewport);
  driver.DrawWindow(gpu.window);
  testing::Image picture{kSize, kSize, driver.ReadPixels(*viewport->GetTarget())};
  gpu.window->RemoveViewport(id);
  REQUIRE_MESSAGE(picture.rgba.size() == size_t{kSize} * kSize * 4, "nothing was drawn into the viewport");
  return picture;
}

void CheckSamePictures(const std::string& name, const testing::Image& a, const testing::Image& b) {
  // The same GPU drew both, so only rounding differs (e.g. edges where a vertex moved by an ulp).
  const testing::CompareSettings settings{.channel_tolerance = 3, .max_bad_fraction = 0.002};
  const testing::CompareResult result = testing::CompareImages(a, b, settings);
  const std::filesystem::path output = WLW_GPU_OUTPUT;
  if (!result.passed) {
    std::filesystem::create_directories(output);
    testing::SavePng((output / (name + ".a.png")).string(), a);
    testing::SavePng((output / (name + ".b.png")).string(), b);
    testing::SavePng((output / (name + ".diff.png")).string(), result.diff);
  }
  INFO(name << ": " << result.bad_pixels << " pixels differ by more than " << settings.channel_tolerance << " ("
            << result.bad_fraction * 100.0 << "%, allowed " << settings.max_bad_fraction * 100.0
            << "%), largest difference " << result.max_difference << "; pictures in " << output.string());
  CHECK(result.passed);
}

void CheckGolden(const std::string& name, const std::shared_ptr<scene::Viewport>& viewport) {
  const testing::Image actual = Render(viewport);
  rendering::RenderingDriver& driver = *GetGpu().engine->GetRenderingDriver();

  const std::filesystem::path reference_path = std::filesystem::path(WLW_GPU_REFERENCES) / (name + ".png");
  const std::filesystem::path output = WLW_GPU_OUTPUT;
  std::filesystem::create_directories(output);

  if (g_update) {
    std::filesystem::create_directories(reference_path.parent_path());
    REQUIRE(testing::SavePng(reference_path.string(), actual));
    MESSAGE("updated " << reference_path.string());
    return;
  }

  testing::Image reference;
  if (!testing::LoadPng(reference_path.string(), reference)) {
    testing::SavePng((output / (name + ".actual.png")).string(), actual);
    FAIL("no reference " << reference_path.string() << " (run wlw_gpu_tests --update on llvmpipe, or take CI's "
                         << "gpu_output artifact); this run's picture is in " << output.string());
  }

  const testing::CompareSettings settings = SettingsFor(driver.GpuName());
  const testing::CompareResult result = testing::CompareImages(actual, reference, settings);
  if (!result.passed) {
    testing::SavePng((output / (name + ".actual.png")).string(), actual);
    testing::SavePng((output / (name + ".diff.png")).string(), result.diff);
  }
  INFO(name << ": " << result.bad_pixels << " pixels differ by more than " << settings.channel_tolerance << " ("
            << result.bad_fraction * 100.0 << "%, allowed " << settings.max_bad_fraction * 100.0
            << "%), largest difference " << result.max_difference << "; pictures in " << output.string());
  CHECK(result.passed);
}

}  // namespace wlw::gpu_tests
