#include "snake_app.h"

#include <algorithm>
#include <cmath>
#include <random>
#include <string>

#include <glm/glm.hpp>

#include "wlw/core/log.h"
#include "wlw/utils/primitives.h"

namespace snake {
namespace {

using wlw::core::Color;

std::shared_ptr<wlw::core::Model<wlw::core::Vertex3D>> MakeModel(std::shared_ptr<wlw::core::Mesh<wlw::core::Vertex3D>> mesh) {
  auto model = std::make_shared<wlw::core::Model<wlw::core::Vertex3D>>();
  model->meshes.push_back(std::move(mesh));
  return model;
}

wlw::AppConfig MakeConfig() {
  wlw::AppConfig config;
  config.title = "WLWSnake";
  config.window_size = {1280, 720};
  config.show_stats_in_title = false;  // the title shows the score instead
  return config;
}

uint32_t RandomSeed() { return std::random_device{}(); }

}  // namespace

SnakeApp::SnakeApp() : Application(MakeConfig()) {}

void SnakeApp::OnStart() {
  using namespace wlw;

  rendering::Lighting sun;
  sun.type = rendering::LightType::Directional;
  // Tilted and with a tight highlight: seen from straight above, an overhead sun would put a
  // specular highlight on every top face and wash the colours out.
  sun.direction = {-0.5f, -1.0f, -0.7f};
  sun.ambient_strength = 0.35f;
  sun.shininess = 64.0f;
  material_ = rendering::Material::Create();
  material_->SetLighting(sun);

  head_model_ = MakeModel(utils::createCubeWithNormals(1.0f, Color{0.95f, 0.80f, 0.15f, 1.0f}));
  dead_head_model_ = MakeModel(utils::createCubeWithNormals(1.0f, Color{0.85f, 0.15f, 0.15f, 1.0f}));
  body_model_ = MakeModel(utils::createCubeWithNormals(1.0f, Color{0.55f, 0.85f, 0.25f, 1.0f}));
  food_model_ = MakeModel(utils::createGemWithNormals(0.5f, 0.8f));

  BuildBoard();

  food_node_ = std::make_shared<scene::Node3D>();
  food_node_->SetModel(food_model_);
  food_node_->SetMaterial(material_);
  Window().GetScreen().GetScene()->AddNode(food_node_);

  particle_node_ = std::make_shared<scene::Node3D>();
  particle_node_->SetModel(MakeModel(utils::createCubeWithNormals(1.0f)));  // white: tinted per particle
  particle_node_->SetMaterial(material_);
  particle_node_->SetInstances({});
  Window().GetScreen().GetScene()->AddNode(particle_node_);

  SetupCameras();

  Restart();
  WLW_LOG_INFO("Steer: arrows / swipe.  Restart after game over: Space / tap.  Interpolation: I.  "
               "Projection: P.  Quit: Esc / Back.");
}

void SnakeApp::SetupCameras() {
  using namespace wlw;
  const float center = (kGridSize - 1) / 2.0f;  // cells are 1x1 and centred on integer coordinates

  // Main camera: straight down at the board, no perspective. Screen "up" is -z on the board, so
  // the up vector is {0, 0, -1}; the usual {0, 1, 0} would be parallel to the view direction.
  ortho_camera_ = scene::OrthographicCamera::Create();
  ortho_camera_->SetPosition({center, 30.0f, center});
  ortho_camera_->SetTarget({center, 0.0f, center});
  ortho_camera_->SetUp({0.0f, 0.0f, -1.0f});
  const float board_with_margin = kGridSize + 2.0f;
  ortho_camera_->SetVisibleArea(board_with_margin, board_with_margin);  // fitted to any aspect ratio
  ortho_camera_->SetClipPlanes(1.0f, 60.0f);

  // For comparison (P key): the earlier tilted perspective view.
  auto center_node = std::make_shared<scene::Node3D>();
  center_node->SetPosition({center, 0.0f, center});
  perspective_camera_ = scene::FollowCamera::Create(center_node);
  FitPerspectiveCameraToScreen();

  Window().GetScreen().SetCamera(ortho_camera_);
}

void SnakeApp::BuildBoard() {
  auto tile_a = MakeModel(wlw::utils::createCubeWithNormals(1.0f, Color{0.20f, 0.45f, 0.25f, 1.0f}));
  auto tile_b = MakeModel(wlw::utils::createCubeWithNormals(1.0f, Color{0.24f, 0.52f, 0.29f, 1.0f}));
  for (int z = 0; z < kGridSize; ++z) {
    for (int x = 0; x < kGridSize; ++x) {
      auto tile = std::make_shared<wlw::scene::Node3D>();
      tile->SetModel((x + z) % 2 == 0 ? tile_a : tile_b);
      tile->SetMaterial(material_);
      tile->SetScale({1.0f, 0.1f, 1.0f});
      tile->SetPosition({(float)x, -0.05f, (float)z});
      Window().GetScreen().GetScene()->AddNode(tile);
    }
  }
}

void SnakeApp::EatBurst() {
  // The head is where the food was (the game has already spawned new food).
  const Cell head = game_.Body().front();
  wlw::fx::BurstSettings burst;
  burst.count_min = 14;
  burst.count_max = 20;
  burst.spread_degrees = 70.0f;
  burst.speed_min = 3.0f;
  burst.speed_max = 6.0f;
  burst.size_start = 0.22f;
  burst.color = {0.95f, 0.85f, 0.3f, 1.0f};
  particles_.Burst(burst, {static_cast<float>(head.x), 0.6f, static_cast<float>(head.z)});
}

void SnakeApp::Restart() {
  game_.Reset(RandomSeed());
  move_timer_ = 0.0f;
  UpdateTitle();
}

// ---------------------------------------------------------------------------------------------
// Simulation: advance the game on its own tick, built on the engine's fixed step.
// ---------------------------------------------------------------------------------------------
void SnakeApp::OnFixedUpdate(float dt) {
  if (game_.GetState() != SnakeGame::State::Playing) return;

  move_timer_ += dt;
  while (move_timer_ >= game_.MoveInterval()) {
    move_timer_ -= game_.MoveInterval();
    switch (game_.Step()) {
      case SnakeGame::StepResult::Ate:
        WLW_LOG_INFO("Yum! Score %d", game_.Score());
        EatBurst();
        UpdateTitle();
        break;
      case SnakeGame::StepResult::Died:
        WLW_LOG_INFO("Game over - score %d. Space / tap to restart.", game_.Score());
        move_timer_ = 0.0f;
        UpdateTitle();
        return;
      case SnakeGame::StepResult::Moved:
        break;
    }
  }
}

// ---------------------------------------------------------------------------------------------
// Per frame: input, camera, cosmetic animation.
// ---------------------------------------------------------------------------------------------
void SnakeApp::OnUpdate(float dt) {
  ReadInput();
  FitPerspectiveCameraToScreen();  // the orthographic camera fits itself (SetVisibleArea)
  food_spin_ += 120.0f * dt;  // degrees per second: purely visual, so per-frame dt is fine
  particles_.Update(dt);
}

void SnakeApp::ReadInput() {
  using wlw::input::Key;
  using wlw::input::Swipe;
  const wlw::input::Input& in = Input();

  if (in.WasKeyPressed(Key::Escape)) Quit();
  if (in.WasKeyPressed(Key::I)) {
    interpolate_ = !interpolate_;
    WLW_LOG_INFO("Interpolation %s", interpolate_ ? "ON (smooth)" : "OFF (cell to cell)");
  }
  if (in.WasKeyPressed(Key::P)) {
    orthographic_ = !orthographic_;
    if (orthographic_) {
      Window().GetScreen().SetCamera(ortho_camera_);
    } else {
      Window().GetScreen().SetCamera(perspective_camera_);
    }
    WLW_LOG_INFO("Projection: %s", orthographic_ ? "orthographic (top-down)" : "perspective (tilted)");
  }

  if (game_.GetState() == SnakeGame::State::GameOver) {
    if (in.WasKeyPressed(Key::Space) || in.WasKeyPressed(Key::Enter) || in.WasTapped()) Restart();
    return;
  }

  if (in.WasKeyPressed(Key::Up)) game_.QueueTurn({0, -1});
  if (in.WasKeyPressed(Key::Down)) game_.QueueTurn({0, 1});
  if (in.WasKeyPressed(Key::Left)) game_.QueueTurn({-1, 0});
  if (in.WasKeyPressed(Key::Right)) game_.QueueTurn({1, 0});
  // Screen "up" is -z on the board in both camera views.
  for (Swipe swipe : in.Swipes()) {
    switch (swipe) {
      case Swipe::Up: game_.QueueTurn({0, -1}); break;
      case Swipe::Down: game_.QueueTurn({0, 1}); break;
      case Swipe::Left: game_.QueueTurn({-1, 0}); break;
      case Swipe::Right: game_.QueueTurn({1, 0}); break;
    }
  }
}

// Keeps the whole board visible in the perspective view on any aspect ratio. With perspective the
// visible area depends on distance and field of view, so the camera backs off until the board fits.
void SnakeApp::FitPerspectiveCameraToScreen() {
  const wlw::core::Vector2 size = Window().GetSize();
  if (size.x <= 0 || size.y <= 0) return;
  const float aspect = size.x / size.y;
  const float vertical_fov = glm::radians(45.0f);  // FollowCamera's field of view
  const float horizontal_fov = 2.0f * std::atan(std::tan(vertical_fov / 2.0f) * aspect);
  const float half_board = kGridSize / 2.0f + 1.0f;  // + margin
  const float distance = half_board / std::tan(std::min(vertical_fov, horizontal_fov) / 2.0f);
  const glm::vec3 direction = glm::normalize(glm::vec3(0.0f, 22.0f, 9.0f));  // ~68 degrees down
  perspective_camera_->SetOffset({direction.x * distance, direction.y * distance, direction.z * distance});
}

// ---------------------------------------------------------------------------------------------
// Presentation: copy the (interpolated) game state into scene nodes.
// ---------------------------------------------------------------------------------------------
void SnakeApp::OnRender(float alpha) {
  float t = 1.0f;
  if (interpolate_ && game_.GetState() == SnakeGame::State::Playing) {
    t = std::clamp((move_timer_ + alpha * Config().fixed_timestep) / game_.MoveInterval(), 0.0f, 1.0f);
  }
  SyncSnakeNodes(t);

  const Cell food = game_.Food();
  const float bob = 0.1f * std::sin(glm::radians(food_spin_ * 2.0f));
  food_node_->SetPosition({(float)food.x, 0.55f + bob, (float)food.z});
  food_node_->SetRotation({0.0f, food_spin_, 0.0f});

  particles_.BuildInstances(particle_instances_);
  particle_node_->SetInstances(particle_instances_);
}

void SnakeApp::SyncSnakeNodes(float t) {
  const auto& body = game_.Body();
  const auto& previous = game_.PreviousBody();
  const bool dead = game_.GetState() == SnakeGame::State::GameOver;

  for (size_t i = 0; i < body.size(); ++i) {
    NodePtr node = AcquireSegmentNode(i);
    // A segment glides from where it was before the last step to where it is now. A segment that
    // just appeared (the snake grew) has no previous position: it simply sits at the tail.
    const Cell to = body[i];
    const Cell from = i < previous.size() ? previous[i] : to;
    node->SetPosition({from.x + (to.x - from.x) * t, 0.45f, from.z + (to.z - from.z) * t});

    if (i == 0) {
      node->SetModel(dead ? dead_head_model_ : head_model_);
      node->SetScale({0.95f, 0.95f, 0.95f});
    } else {
      node->SetModel(body_model_);
      const float taper = 0.85f - 0.25f * static_cast<float>(i) / static_cast<float>(body.size());
      node->SetScale({taper, taper, taper});
    }
    node->SetVisible(true);
  }
  // Pool nodes beyond the current length (after a restart) are hidden, not destroyed.
  for (size_t i = body.size(); i < segment_nodes_.size(); ++i) {
    segment_nodes_[i]->SetVisible(false);
  }
}

SnakeApp::NodePtr SnakeApp::AcquireSegmentNode(size_t index) {
  while (segment_nodes_.size() <= index) {
    auto node = std::make_shared<wlw::scene::Node3D>();
    node->SetModel(body_model_);  // meshes/models are shared by all segments: one GPU copy
    node->SetMaterial(material_);
    Window().GetScreen().GetScene()->AddNode(node);
    segment_nodes_.push_back(node);
  }
  return segment_nodes_[index];
}

void SnakeApp::UpdateTitle() {
  std::string title = "WLWSnake | Score " + std::to_string(game_.Score());
  if (game_.GetState() == SnakeGame::State::GameOver) title += " | GAME OVER - Space to restart";
  Window().SetTitle(title);
}

}  // namespace snake

#if WLW_DEBUG_UI
#include "imgui.h"

namespace snake {

// Debug panel (F1 / "Debug" button): a small example of the engine's debug UI.
void SnakeApp::OnDebugUI() {
  ImGui::Begin("Snake");
  ImGui::Text("%.0f fps (%.2f ms)", Fps(), FrameMs());
  ImGui::Text("Score %d, length %zu", game_.Score(), game_.Body().size());
  ImGui::Text("Move interval %.3f s", game_.MoveInterval());
  ImGui::Checkbox("Interpolate", &interpolate_);
  if (ImGui::Checkbox("Orthographic", &orthographic_)) {
    if (orthographic_) {
      Window().GetScreen().SetCamera(ortho_camera_);
    } else {
      Window().GetScreen().SetCamera(perspective_camera_);
    }
  }
  if (ImGui::Button("Restart")) Restart();
  ImGui::End();
}

}  // namespace snake
#endif
