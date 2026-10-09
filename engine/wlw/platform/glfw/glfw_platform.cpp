// Desktop platform layer (Windows, macOS, Linux): GLFW for windows, GL contexts and input,
// glad for loading OpenGL 3.3 core functions.
#ifdef WLW_PLATFORM_GLFW

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>

#include "wlw/rendering/gl.h"  // glad must come before GLFW
#include <GLFW/glfw3.h>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>  // GetModuleFileNameW (after glad/GLFW, which set up the GL macros)
#elif defined(__APPLE__)
#include <mach-o/dyld.h>
#endif

#include "wlw/platform/platform.h"
#include "wlw/scene/window.h"

namespace wlw::platform {
namespace {
bool g_alive = false;

void GlfwErrorCallback(int error, const char* description) {
  WLW_LOG_ERROR("GLFW error %d: %s", error, description);
}

// The folder of the running executable: where the build copies the game's assets.
std::filesystem::path ExecutableDirectory() {
#if defined(_WIN32)
  wchar_t buffer[MAX_PATH];
  const DWORD length = GetModuleFileNameW(nullptr, buffer, MAX_PATH);
  if (length == 0 || length == MAX_PATH) return {};
  return std::filesystem::path(buffer).parent_path();
#elif defined(__APPLE__)
  char buffer[4096];
  uint32_t size = sizeof(buffer);
  if (_NSGetExecutablePath(buffer, &size) != 0) return {};
  return std::filesystem::path(buffer).parent_path();
#else
  std::error_code error;
  const std::filesystem::path exe = std::filesystem::read_symlink("/proc/self/exe", error);
  return error ? std::filesystem::path() : exe.parent_path();
#endif
}
}  // namespace

bool Init() {
  if (g_alive) return true;
  glfwSetErrorCallback(GlfwErrorCallback);
  if (!glfwInit()) return false;
  g_alive = true;

  // Every window we create gets an OpenGL 3.3 core context.
  glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
  glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
  glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
#ifdef __APPLE__
  glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
#endif
  return true;
}

void Shutdown() {
  if (!g_alive) return;
  glfwTerminate();  // also destroys any windows still open
  g_alive = false;
}

bool IsAlive() { return g_alive; }

bool HasCurrentGLContext() { return g_alive && glfwGetCurrentContext() != nullptr; }

bool LoadGLFunctions() { return gladLoadGLLoader(reinterpret_cast<GLADloadproc>(glfwGetProcAddress)) != 0; }

std::optional<std::vector<uint8_t>> ReadFile(const std::string& path) {
  std::ifstream file(path, std::ios::binary);
  if (!file && std::filesystem::path(path).is_relative()) {
    file.open(ExecutableDirectory() / path, std::ios::binary);  // e.g. launched from elsewhere
  }
  if (!file) return std::nullopt;
  return std::vector<uint8_t>(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
}

SafeAreaInsets GetSafeAreaInsets() { return {}; }  // desktop windows have no cutouts or system bars

bool MoveToBackground() { return false; }  // no such thing on desktop: the app quits instead

void Vibrate(int, float) {}  // desktops don't vibrate

void WriteLog(LogLevel level, const char* message) {
  static const char* kPrefix[] = {"", "[warning] ", "[error] "};
  FILE* out = level == LogLevel::Error ? stderr : stdout;
  std::fprintf(out, "%s%s\n", kPrefix[static_cast<int>(level)], message);
  std::fflush(out);
}

}  // namespace wlw::platform

namespace wlw::scene {
namespace {

input::Key MapKey(int key) {
  switch (key) {
    case GLFW_KEY_UP: return input::Key::Up;
    case GLFW_KEY_DOWN: return input::Key::Down;
    case GLFW_KEY_LEFT: return input::Key::Left;
    case GLFW_KEY_RIGHT: return input::Key::Right;
    case GLFW_KEY_SPACE: return input::Key::Space;
    case GLFW_KEY_ENTER: return input::Key::Enter;
    case GLFW_KEY_ESCAPE: return input::Key::Escape;
    case GLFW_KEY_W: return input::Key::W;
    case GLFW_KEY_A: return input::Key::A;
    case GLFW_KEY_S: return input::Key::S;
    case GLFW_KEY_D: return input::Key::D;
    case GLFW_KEY_I: return input::Key::I;
    case GLFW_KEY_P: return input::Key::P;
    case GLFW_KEY_F1: return input::Key::F1;
    default: return input::Key::Unknown;
  }
}

class GlfwWindow : public Window {
 public:
  GlfwWindow(const core::Vector2& size, const std::string& title) : title_(title) { size_ = size; }

  ~GlfwWindow() override {
    // If the platform was already shut down, glfwTerminate() destroyed this window for us.
    if (window_ != nullptr && platform::IsAlive()) {
      glfwDestroyWindow(window_);
    }
    window_ = nullptr;
  }

  bool Initialize(Window* share_with) override {
    if (window_ != nullptr) {
      WLW_LOG_WARN("Window '%s' is already initialized", title_.c_str());
      return true;
    }
    GLFWwindow* share = share_with ? static_cast<GlfwWindow*>(share_with)->window_ : nullptr;
    window_ = glfwCreateWindow((int)size_.x, (int)size_.y, title_.c_str(), nullptr, share);
    if (window_ == nullptr) {
      WLW_LOG_ERROR("Failed to create GLFW window '%s'", title_.c_str());
      return false;
    }

    // Route GLFW callbacks to this object.
    glfwSetWindowUserPointer(window_, this);
    glfwSetKeyCallback(window_, &GlfwWindow::OnKey);
    glfwSetMouseButtonCallback(window_, &GlfwWindow::OnMouseButton);
    glfwSetCursorPosCallback(window_, &GlfwWindow::OnCursorPos);
    glfwSetFramebufferSizeCallback(window_, &GlfwWindow::OnFramebufferSize);

    int fb_w = 0, fb_h = 0;
    glfwGetFramebufferSize(window_, &fb_w, &fb_h);  // pixels (can differ from size_ on HiDPI)
    size_ = {(float)fb_w, (float)fb_h};
    UpdateInputSurface();

    glfwMakeContextCurrent(window_);
    glfwSwapInterval(1);  // vsync
    return true;
  }

  void MakeContextCurrent() override {
    if (window_ && glfwGetCurrentContext() != window_) glfwMakeContextCurrent(window_);
  }

  void ProcessEvents() override { glfwPollEvents(); }

  void SwapBuffers() override {
    if (window_) glfwSwapBuffers(window_);
  }

  bool ShouldClose() const override { return window_ == nullptr || glfwWindowShouldClose(window_); }

  void RequestClose() override {
    if (window_) glfwSetWindowShouldClose(window_, GLFW_TRUE);
  }

  void SetTitle(const std::string& title) override {
    if (window_) glfwSetWindowTitle(window_, title.c_str());
  }

 private:
  static GlfwWindow* From(GLFWwindow* w) { return static_cast<GlfwWindow*>(glfwGetWindowUserPointer(w)); }

  static void OnKey(GLFWwindow* w, int key, int /*scancode*/, int action, int /*mods*/) {
    if (action == GLFW_REPEAT) return;
    From(w)->input_.OnKey(MapKey(key), action == GLFW_PRESS);
  }

  static void OnMouseButton(GLFWwindow* w, int button, int action, int /*mods*/) {
    if (button != GLFW_MOUSE_BUTTON_LEFT) return;
    double x = 0, y = 0;
    glfwGetCursorPos(w, &x, &y);
    if (action == GLFW_PRESS) From(w)->input_.OnPointerDown((float)x, (float)y);
    else From(w)->input_.OnPointerUp((float)x, (float)y);
  }

  static void OnCursorPos(GLFWwindow* w, double x, double y) {
    From(w)->input_.OnPointerMove((float)x, (float)y);
    if (glfwGetInputMode(w, GLFW_CURSOR) == GLFW_CURSOR_DISABLED) {
      From(w)->ProcessMouseMovement(x, y);  // mouse-look only when the cursor is captured
    }
  }

  static void OnFramebufferSize(GLFWwindow* w, int width, int height) {
    GlfwWindow* self = From(w);
    self->size_ = {(float)width, (float)height};
    self->UpdateInputSurface();
  }

  void UpdateInputSurface() {
    int w = 0, h = 0;
    glfwGetWindowSize(window_, &w, &h);  // cursor coordinates are in window units, not pixels
    input_.SetSurfaceSize((float)w, (float)h);
  }

  GLFWwindow* window_ = nullptr;
  std::string title_;
};

}  // namespace

std::unique_ptr<Window> Window::Create(const core::Vector2& size, const std::string& title) {
  return std::make_unique<GlfwWindow>(size, title);
}

}  // namespace wlw::scene

#endif  // WLW_PLATFORM_GLFW
