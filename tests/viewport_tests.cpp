// Tests for scenes and viewports (scene/scene.h, scene/viewport.h, Window's viewport list): what is
// shown where, sizes, render scale, update modes. CPU only: no GL context (viewports and their targets are made without
// one).
#include <cmath>
#include <memory>
#include <string>
#include <vector>

#include "test_helpers.h"
#include "wlw/scene/orthographic_camera.h"
#include "wlw/scene/scene.h"
#include "wlw/scene/viewport.h"
#include "wlw/scene/window.h"

TEST_SUITE_BEGIN("viewport");

using namespace wlw;

namespace {

// A window without an OS window: only the shared (platform-free) part is exercised.
class FakeWindow : public scene::Window {
 public:
  bool Initialize(Window*) override { return true; }
  void MakeContextCurrent() override {}
  void ProcessEvents() override {}
  void SwapBuffers() override {}
  bool ShouldClose() const override { return false; }
  void RequestClose() override {}
  void SetTitle(const std::string&) override {}
};

std::shared_ptr<scene::Viewport> Offscreen(const std::string& name = "Viewport") {
  return scene::Viewport::Create({.name = name});  // no GPU needed: the renderer makes the framebuffer
}

}  // namespace

TEST_CASE("scene") {
  auto scene = scene::Scene::Create();
  auto a = std::make_shared<scene::Node3D>();
  auto b = std::make_shared<scene::Node3D>();
  auto child = std::make_shared<scene::Node3D>();
  a->AddNode(child);
  const int id_a = scene->AddNode(a);
  scene->AddNode(b);
  CHECK(scene->NodeCount() == 2);

  // Top-level nodes first, then their children.
  std::vector<scene::Node3D*> visited;
  scene->IterateOverNodes([&](const std::shared_ptr<scene::Node3D>& node) { visited.push_back(node.get()); });
  CHECK(visited.size() == 3);
  CHECK(visited.back() == child.get());

  scene->RemoveNode(id_a);
  CHECK(scene->NodeCount() == 1);
  scene->Clear();
  CHECK((scene->NodeCount() == 0 && scene->GetSkybox() == nullptr));
  CHECK(scene->AddNode(a) == 0);  // ids start over after Clear
}

TEST_CASE("viewport shows a shared scene") {
  auto world = scene::Scene::Create();
  auto main_view = Offscreen();
  auto minimap = Offscreen();
  CHECK(
      (main_view->GetScene() != nullptr && main_view->GetScene() != minimap->GetScene()));  // each starts with its own
  main_view->SetScene(world);
  minimap->SetScene(world);
  world->AddNode(std::make_shared<scene::Node3D>());
  CHECK((main_view->GetScene()->NodeCount() == 1 && minimap->GetScene()->NodeCount() == 1));  // same world

  auto camera = std::make_shared<scene::OrthographicCamera>();
  minimap->SetCamera(camera);
  CHECK(minimap->GetCamera() == camera);
  CHECK(main_view->GetCamera() != nullptr);  // a default camera until SetCamera

  CHECK((!main_view->IsScreen() && main_view->GetTarget() != nullptr));
  scene::Viewport screen(nullptr, {});
  CHECK((screen.IsScreen() && screen.GetTexture() == nullptr));
}

TEST_CASE("viewport size") {
  auto view = Offscreen();
  // Default: the screen's size.
  CHECK(view->PixelSize({1080.0f, 2400.0f}) == glm::ivec2(1080, 2400));
  view->SetSizeFromScreen(0.5f);
  CHECK(view->PixelSize({1080.0f, 2400.0f}) == glm::ivec2(540, 1200));
  CHECK(view->PixelSize({2400.0f, 1080.0f}) == glm::ivec2(1200, 540));  // follows rotation
  CHECK(view->PixelSize({0.0f, 0.0f}) == glm::ivec2(0, 0));             // minimized: not drawn
  view->SetSizeFromScreen(0.0001f);
  CHECK(view->PixelSize({100.0f, 100.0f}) == glm::ivec2(1, 1));  // never smaller than a pixel
  view->SetSize(256, 128);
  CHECK(view->PixelSize({1080.0f, 2400.0f}) == glm::ivec2(256, 128));
  CHECK(view->PixelSize({0.0f, 0.0f}) == glm::ivec2(256, 128));  // fixed sizes don't depend on the screen
  view->SetSize(0, -3);
  CHECK(view->PixelSize({}) == glm::ivec2(1, 1));
}

TEST_CASE("render scale sizes") {
  auto view = Offscreen();
  // 1 by default: the 3D at the viewport's own size.
  CHECK(view->GetRenderScale() == 1.0f);
  CHECK(view->RenderSize({1080, 2400}) == glm::ivec2(1080, 2400));
  view->SetRenderScale(0.75f);
  CHECK(view->RenderSize({1080, 2400}) == glm::ivec2(810, 1800));
  view->SetRenderScale(0.5f);
  CHECK(view->RenderSize({1081, 2400}) == glm::ivec2(541, 1200));  // rounded
  CHECK(view->RenderSize({0, 0}) == glm::ivec2(0, 0));             // no size (minimized): nothing to draw
  CHECK(view->RenderSize({1, 1}) == glm::ivec2(1, 1));             // never below a pixel
  // Clamped to 0.25..2 (above 1 = supersampling); not a number = 1.
  view->SetRenderScale(0.1f);
  CHECK(view->GetRenderScale() == scene::Viewport::kMinRenderScale);
  view->SetRenderScale(5.0f);
  CHECK(view->GetRenderScale() == scene::Viewport::kMaxRenderScale);
  CHECK(view->RenderSize({100, 50}) == glm::ivec2(200, 100));
  view->SetRenderScale(std::nanf(""));
  CHECK(view->GetRenderScale() == 1.0f);

  // An override (the r.render_scale cvar sets the screen's) wins while it is set; 0 = none.
  view->SetRenderScale(0.8f);
  view->SetRenderScaleOverride(0.5f);
  CHECK((view->EffectiveRenderScale() == 0.5f && view->GetRenderScale() == 0.8f));
  CHECK(view->RenderSize({200, 100}) == glm::ivec2(100, 50));
  view->SetRenderScale(0.9f);  // the game changes its own: still overridden
  CHECK(view->EffectiveRenderScale() == 0.5f);
  view->SetRenderScaleOverride(0.0f);
  CHECK((view->GetRenderScaleOverride() == 0.0f && view->EffectiveRenderScale() == 0.9f));  // the game's again
  view->SetRenderScaleOverride(9.0f);
  CHECK(view->EffectiveRenderScale() == scene::Viewport::kMaxRenderScale);  // clamped too
}

TEST_CASE("update modes") {
  auto view = Offscreen();
  const glm::ivec2 size{100, 50};
  // Always: every frame.
  CHECK((view->BeginDraw(size) && view->BeginDraw(size)));
  CHECK(!view->BeginDraw({0, 0}));  // nothing to draw into

  // Once: the next frame only, then it keeps its picture (and reports Disabled).
  view->SetUpdateMode(scene::Viewport::UpdateMode::Once);
  CHECK(view->BeginDraw(size));
  CHECK(view->GetUpdateMode() == scene::Viewport::UpdateMode::Disabled);
  CHECK((!view->BeginDraw(size) && !view->BeginDraw(size)));
  // A resize throws the kept picture away: it's drawn once more at the new size.
  CHECK(view->BeginDraw({200, 100}));
  CHECK(!view->BeginDraw({200, 100}));

  // Disabled from the start: never drawn, not even after a resize (there was no picture to keep).
  auto never = Offscreen();
  never->SetUpdateMode(scene::Viewport::UpdateMode::Disabled);
  CHECK((!never->BeginDraw(size) && !never->BeginDraw({300, 300})));
  // Once, again later: draws one more frame.
  never->SetUpdateMode(scene::Viewport::UpdateMode::Once);
  CHECK((never->BeginDraw({300, 300}) && !never->BeginDraw({300, 300})));
}

TEST_CASE("clear color") {
  auto view = Offscreen();
  CHECK((view->GetClearColor().has_value() && view->GetClearColor()->a == 0.0f));  // transparent by default
  view->SetClearColor(std::nullopt);                                               // keep the last picture (trails)
  CHECK(!view->GetClearColor().has_value());
}

TEST_CASE("window viewports") {
  FakeWindow window;
  CHECK((window.GetScreen().IsScreen() && window.GetScreen().GetScene() != nullptr));
  CHECK(window.GetScreen().GetClearColor()->a == 1.0f);  // the screen is opaque

  auto first = Offscreen("first");
  auto second = Offscreen("second");
  auto third = Offscreen("third");
  window.AddViewport(first);
  const int id_second = window.AddViewport(second);
  window.AddViewport(third);
  // Drawn in the order added.
  CHECK(window.GetViewports().size() == 3);
  CHECK((window.GetViewports()[0].second == first && window.GetViewports()[2].second == third));
  window.RemoveViewport(id_second);
  CHECK((window.GetViewports().size() == 2 && window.GetViewports()[1].second == third));

  // Shutdown: scenes and viewports are let go (the game may still hold its own references).
  window.GetScreen().GetScene()->AddNode(std::make_shared<scene::Node3D>());
  window.ReleaseScenes();
  CHECK((window.GetViewports().empty() && window.GetScreen().GetScene() == nullptr));
}

TEST_CASE("canvas matches screen units") {
  // A half-resolution viewport's canvas has the same size in units as the screen's, so the game
  // can draw at the same coordinates (just with fewer pixels).
  auto view = Offscreen();
  view->SetSizeFromScreen(0.5f);
  const glm::vec2 screen{1080.0f, 2400.0f};
  const glm::vec2 reference{360.0f, 640.0f};
  ui::Canvas screen_canvas;
  screen_canvas.BeginFrame(screen, reference);
  view->GetCanvas().BeginFrame(glm::vec2(view->PixelSize(screen)), reference);
  CHECK(Near(view->GetCanvas().Size().x, screen_canvas.Size().x, 0.01f));
  CHECK(Near(view->GetCanvas().Size().y, screen_canvas.Size().y, 0.01f));
  CHECK(Near(view->GetCanvas().PixelsPerUnit() * 2.0f, screen_canvas.PixelsPerUnit(), 0.01f));
}

TEST_CASE("canvas layers") {
  using scene::CanvasMode;
  auto view = Offscreen();
  ui::Canvas& ui = view->GetCanvas();  // the default layer: Overlay, order 0
  CHECK(view->CanvasLayerCount() == 1);

  ui::Canvas& popup = view->AddCanvasLayer({.mode = CanvasMode::Overlay, .order = 10});
  ui::Canvas& glow = view->AddCanvasLayer({.mode = CanvasMode::Picture});
  ui::Canvas& backdrop = view->AddCanvasLayer({.mode = CanvasMode::Overlay, .order = -5});
  ui::Canvas& hud = view->AddCanvasLayer({.mode = CanvasMode::Overlay});  // ties: after the default one
  ui::Canvas& background = view->AddCanvasLayer({.mode = CanvasMode::Picture, .order = -1});
  CHECK(view->CanvasLayerCount() == 6);

  auto order_of = [&](CanvasMode mode) {
    std::vector<const ui::Canvas*> drawn;
    view->ForEachCanvasLayer(mode, [&](const ui::Canvas& canvas) { drawn.push_back(&canvas); });
    return drawn;
  };
  // Picture layers by order (they go into the picture, before the post passes)...
  CHECK(order_of(CanvasMode::Picture) == std::vector<const ui::Canvas*>({&background, &glow}));
  // ...Overlay layers by order, equal orders in the order added (the default one first).
  CHECK(order_of(CanvasMode::Overlay) == std::vector<const ui::Canvas*>({&backdrop, &ui, &hud, &popup}));

  // Every layer is started each frame at the viewport's size, in the same units.
  view->BeginCanvases({540.0f, 1200.0f}, {360.0f, 640.0f});
  CHECK((Near(glow.Size().x, ui.Size().x) && Near(popup.Size().y, ui.Size().y)));
  glow.FillRect({{0, 0}, {10, 10}}, {1, 1, 1, 1});
  CHECK((glow.QuadCount() == 1 && ui.QuadCount() == 0));  // separate draw lists
  view->BeginCanvases({540.0f, 1200.0f}, {360.0f, 640.0f});
  CHECK(glow.QuadCount() == 0);

  // References stay valid as layers come and go; the default layer can't be removed.
  view->RemoveCanvasLayer(hud);
  view->RemoveCanvasLayer(ui);
  CHECK(view->CanvasLayerCount() == 5);
  CHECK(order_of(CanvasMode::Overlay) == std::vector<const ui::Canvas*>({&backdrop, &ui, &popup}));
  CHECK(&view->GetCanvas() == &ui);
}

TEST_SUITE_END();
