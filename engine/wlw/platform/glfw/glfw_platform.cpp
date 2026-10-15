// Desktop platform layer (Windows, macOS, Linux): GLFW for windows, GL contexts and input,
// glad for loading OpenGL 3.3 core functions.
#ifdef WLW_PLATFORM_GLFW

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>

#include "wlw/rendering/gl.h"  // glad must come before GLFW
#include <GLFW/glfw3.h>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>  // GetModuleFileNameW (after glad/GLFW, which set up the GL macros)
#include <io.h>       // _commit
#elif defined(__APPLE__)
#include <mach-o/dyld.h>
#endif
#if !defined(_WIN32)
#include <unistd.h>  // fsync
#endif

#include "wlw/core/frame_pacing.h"
#include "wlw/core/profile.h"
#include "wlw/platform/bridge.h"
#include "wlw/platform/platform.h"
#include "wlw/scene/window.h"

namespace wlw::platform {
namespace {
bool g_alive = false;

void GlfwErrorCallback(int error, const char* description) {
  WLW_LOG(LogPlatform, Error, "GLFW error %d: %s", error, description);
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

bool CurrentContextIsES() {
  GLFWwindow* context = g_alive ? glfwGetCurrentContext() : nullptr;
  return context != nullptr && glfwGetWindowAttrib(context, GLFW_CLIENT_API) == GLFW_OPENGL_ES_API;
}

bool LoadGLFunctions() {
  if (gladLoadGLLoader(reinterpret_cast<GLADloadproc>(glfwGetProcAddress)) == 0) return false;
  // glad loads by version number, and an OpenGL ES context's "3.0" means GL 3.0's functions: the
  // uniform blocks (GL 3.1, ES 3.0) come by name then. (Mesa reports ES 3.2, which covers them.)
  if (CurrentContextIsES() && glad_glGetUniformBlockIndex == nullptr) {
    glad_glGetUniformBlockIndex =
        reinterpret_cast<PFNGLGETUNIFORMBLOCKINDEXPROC>(glfwGetProcAddress("glGetUniformBlockIndex"));
    glad_glUniformBlockBinding =
        reinterpret_cast<PFNGLUNIFORMBLOCKBINDINGPROC>(glfwGetProcAddress("glUniformBlockBinding"));
  }
  return true;
}

void* GetGLProcAddress(const char* name) { return reinterpret_cast<void*>(glfwGetProcAddress(name)); }

std::optional<std::vector<uint8_t>> ReadFile(const std::string& path) {
  WLW_PROFILE_SCOPE("Read file");
  WLW_PROFILE_TEXT(path.data(), path.size());
  std::ifstream file(path, std::ios::binary);
  if (!file && std::filesystem::path(path).is_relative()) {
    file.open(ExecutableDirectory() / path, std::ios::binary);  // e.g. launched from elsewhere
  }
  if (!file) return std::nullopt;
  return std::vector<uint8_t>(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
}

// ---- Save data ----
namespace {
std::filesystem::path FromUtf8(const std::string& path) {
  return std::filesystem::path(std::u8string(path.begin(), path.end()));
}
std::string ToUtf8(const std::filesystem::path& path) {
  const std::u8string text = path.u8string();
  return std::string(text.begin(), text.end());
}
}  // namespace

std::string GetSaveDirectory(const std::string& app_name) {
  std::filesystem::path base;
#if defined(_WIN32)
  if (const wchar_t* appdata = _wgetenv(L"APPDATA")) base = appdata;
#elif defined(__APPLE__)
  if (const char* home = std::getenv("HOME")) base = std::filesystem::path(home) / "Library" / "Application Support";
#else
  if (const char* xdg = std::getenv("XDG_DATA_HOME"); xdg && *xdg) {
    base = xdg;
  } else if (const char* home = std::getenv("HOME")) {
    base = std::filesystem::path(home) / ".local" / "share";
  }
#endif
  if (base.empty()) return {};
  const std::filesystem::path folder = base / FromUtf8(app_name);
  std::error_code error;
  std::filesystem::create_directories(folder, error);
  return std::filesystem::is_directory(folder, error) ? ToUtf8(folder) : std::string();
}

bool WriteFileAtomic(const std::string& path, const std::vector<uint8_t>& data) {
  const std::filesystem::path target = FromUtf8(path);
  std::filesystem::path temp = target;
  temp += ".tmp";
#if defined(_WIN32)
  FILE* file = _wfopen(temp.c_str(), L"wb");
#else
  FILE* file = std::fopen(temp.c_str(), "wb");
#endif
  if (!file) return false;
  bool ok = data.empty() || std::fwrite(data.data(), 1, data.size(), file) == data.size();
  ok = ok && std::fflush(file) == 0;  // C library buffer -> OS
#if defined(_WIN32)
  ok = ok && _commit(_fileno(file)) == 0;  // OS cache -> disk
#else
  ok = ok && fsync(fileno(file)) == 0;
#endif
  std::fclose(file);
  std::error_code error;
  if (ok) std::filesystem::rename(temp, target, error);  // replaces the old file in one step
  if (!ok || error) {
    std::filesystem::remove(temp, error);
    return false;
  }
  return true;
}

std::optional<std::vector<uint8_t>> ReadLocalFile(const std::string& path) {
  std::ifstream file(FromUtf8(path), std::ios::binary);
  if (!file) return std::nullopt;
  return std::vector<uint8_t>(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
}

SafeAreaInsets GetSafeAreaInsets() { return {}; }  // desktop windows have no cutouts or system bars

bool MoveToBackground() { return false; }  // no such thing on desktop: the app quits instead

void Vibrate(int, float) {}  // desktops don't vibrate

// No platform-language side on desktop: games use C++ fakes for SDK features (see bridge.h).
bool HasBridge() { return false; }
bool BridgeCall(const std::string&, const std::string&, const std::string&) { return false; }

void WriteLog(const LogRecord& record) {
  FILE* out = record.level >= LogLevel::Error ? stderr : stdout;
  std::fprintf(out, "%s\n", FormatLogRecord(record).c_str());
  std::fflush(out);
}

// IsDebuggerAttached / BreakIntoDebugger: glfw_debugger.cpp (needs <windows.h>, kept out of this file).

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
    case GLFW_KEY_TAB: return input::Key::Tab;
    case GLFW_KEY_BACKSPACE: return input::Key::Backspace;
    case GLFW_KEY_DELETE: return input::Key::Delete;
    case GLFW_KEY_HOME: return input::Key::Home;
    case GLFW_KEY_END: return input::Key::End;
    case GLFW_KEY_PAGE_UP: return input::Key::PageUp;
    case GLFW_KEY_PAGE_DOWN: return input::Key::PageDown;
    case GLFW_KEY_LEFT_CONTROL:
    case GLFW_KEY_RIGHT_CONTROL: return input::Key::Control;
    case GLFW_KEY_LEFT_SHIFT:
    case GLFW_KEY_RIGHT_SHIFT: return input::Key::Shift;
    case GLFW_KEY_GRAVE_ACCENT: return input::Key::Grave;
    default: return input::Key::Unknown;
  }
}

class GlfwWindow : public Window {
 public:
  GlfwWindow(const core::Vector2& size, const std::string& title, const WindowOptions& options)
      : title_(title), options_(options) {
    size_ = size;
  }

  ~GlfwWindow() override {
    // If the platform was already shut down, glfwTerminate() destroyed this window for us.
    if (window_ != nullptr && platform::IsAlive()) {
      glfwDestroyWindow(window_);
    }
    window_ = nullptr;
  }

  bool Initialize(Window* share_with) override {
    if (window_ != nullptr) {
      WLW_LOG(LogPlatform, Warning, "Window '%s' is already initialized", title_.c_str());
      return true;
    }
    GLFWwindow* share = share_with ? static_cast<GlfwWindow*>(share_with)->window_ : nullptr;
    glfwWindowHint(GLFW_VISIBLE, options_.visible ? GLFW_TRUE : GLFW_FALSE);
    glfwWindowHint(GLFW_DEPTH_BITS, options_.depth ? 24 : 0);  // no depth: no stencil either
    glfwWindowHint(GLFW_STENCIL_BITS, options_.depth ? 8 : 0);
    if (options_.gles) {  // OpenGL ES 3.0 (the profile hint doesn't apply to ES)
      glfwWindowHint(GLFW_CLIENT_API, GLFW_OPENGL_ES_API);
      glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);
    }
    window_ = glfwCreateWindow((int)size_.x, (int)size_.y, title_.c_str(), nullptr, share);
    glfwWindowHint(GLFW_VISIBLE, GLFW_TRUE);
    glfwWindowHint(GLFW_DEPTH_BITS, 24);
    glfwWindowHint(GLFW_STENCIL_BITS, 8);
    glfwWindowHint(GLFW_CLIENT_API, GLFW_OPENGL_API);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    if (window_ == nullptr) {
      WLW_LOG(LogPlatform, Error, "Failed to create GLFW window '%s'", title_.c_str());
      return false;
    }

    // Route GLFW callbacks to this object.
    glfwSetWindowUserPointer(window_, this);
    glfwSetKeyCallback(window_, &GlfwWindow::OnKey);
    glfwSetCharCallback(window_, &GlfwWindow::OnChar);
    glfwSetMouseButtonCallback(window_, &GlfwWindow::OnMouseButton);
    glfwSetCursorPosCallback(window_, &GlfwWindow::OnCursorPos);
    glfwSetFramebufferSizeCallback(window_, &GlfwWindow::OnFramebufferSize);
    glfwSetWindowPosCallback(window_, &GlfwWindow::OnWindowPos);

    int fb_w = 0, fb_h = 0;
    glfwGetFramebufferSize(window_, &fb_w, &fb_h);  // pixels (can differ from size_ on HiDPI)
    size_ = {(float)fb_w, (float)fb_h};
    UpdateInputSurface();

    glfwMakeContextCurrent(window_);
    ApplyPacing();
    return true;
  }

  void MakeContextCurrent() override {
    if (window_ && glfwGetCurrentContext() != window_) glfwMakeContextCurrent(window_);
  }

  void ProcessEvents() override { glfwPollEvents(); }

  void SwapBuffers() override {
    if (!window_) return;
    glfwSwapBuffers(window_);
    if (limiter_.Period() > 0.0) {
      const double now = std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
      if (const double wait = limiter_.Next(now); wait > 0.0) {
        WLW_PROFILE_SCOPE("Frame limiter");
        platform::PreciseSleep(wait);
      }
    }
  }

  // ---- Frame pacing ----
  void SetTargetFps(int fps) override {
    if (fps == target_fps_) return;
    target_fps_ = fps;
    ApplyPacing();
  }
  float RefreshRate() const override { return refresh_hz_; }
  float PacedFps() const override { return static_cast<float>(plan_.fps); }
  std::string PacingMethod() const override { return core::PacingPlanName(plan_); }

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

  static void OnChar(GLFWwindow* w, unsigned int codepoint) { From(w)->input_.OnText(codepoint); }

  static void OnMouseButton(GLFWwindow* w, int button, int action, int /*mods*/) {
    if (button != GLFW_MOUSE_BUTTON_LEFT) return;
    double x = 0, y = 0;
    glfwGetCursorPos(w, &x, &y);
    if (action == GLFW_PRESS)
      From(w)->input_.OnPointerDown((float)x, (float)y);
    else
      From(w)->input_.OnPointerUp((float)x, (float)y);
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

  // Moved, maybe to a monitor with another refresh rate.
  static void OnWindowPos(GLFWwindow* w, int /*x*/, int /*y*/) {
    GlfwWindow* self = From(w);
    if (self->FindRefreshRate() != self->refresh_hz_) self->ApplyPacing();
  }

  // The refresh rate of the monitor showing the window (the one its centre is on), 0 if unknown.
  float FindRefreshRate() const {
    GLFWmonitor* monitor = glfwGetWindowMonitor(window_);  // set when full screen
    if (monitor == nullptr) {
      int x = 0, y = 0, w = 0, h = 0;
      glfwGetWindowPos(window_, &x, &y);
      glfwGetWindowSize(window_, &w, &h);
      const int cx = x + w / 2, cy = y + h / 2;
      int count = 0;
      GLFWmonitor** monitors = glfwGetMonitors(&count);
      for (int i = 0; i < count && monitor == nullptr; ++i) {
        int mx = 0, my = 0;
        glfwGetMonitorPos(monitors[i], &mx, &my);
        const GLFWvidmode* mode = glfwGetVideoMode(monitors[i]);
        if (mode && cx >= mx && cx < mx + mode->width && cy >= my && cy < my + mode->height) monitor = monitors[i];
      }
      if (monitor == nullptr) monitor = glfwGetPrimaryMonitor();
    }
    const GLFWvidmode* mode = monitor ? glfwGetVideoMode(monitor) : nullptr;
    return mode ? static_cast<float>(mode->refreshRate) : 0.0f;
  }

  // The target on this monitor: a vsync interval, or vsync off + the limiter. A hidden window never
  // waits for a screen.
  void ApplyPacing() {
    if (window_ == nullptr) return;
    refresh_hz_ = FindRefreshRate();
    plan_ = options_.visible ? core::PlanPacing(target_fps_, refresh_hz_)
                             : core::PacingPlan{.swap_interval = 0, .limit_seconds = 0.0, .fps = 0.0};
    GLFWwindow* previous = glfwGetCurrentContext();
    if (previous != window_) glfwMakeContextCurrent(window_);  // the swap interval belongs to the context
    glfwSwapInterval(plan_.swap_interval);
    if (previous != window_ && previous != nullptr) glfwMakeContextCurrent(previous);
    limiter_.SetPeriod(plan_.limit_seconds);
    if (options_.visible) {
      WLW_LOG(LogPlatform, Info, "Frame pacing: target %s fps, display %.0f Hz -> %s (%.1f fps)",
              core::TargetFpsName(target_fps_).c_str(), refresh_hz_, core::PacingPlanName(plan_).c_str(), plan_.fps);
    }
  }

  GLFWwindow* window_ = nullptr;
  std::string title_;
  WindowOptions options_;
  float refresh_hz_ = 0.0f;
  core::PacingPlan plan_;
  core::FrameLimiter limiter_;
};

}  // namespace

std::unique_ptr<Window> Window::Create(const core::Vector2& size, const std::string& title,
                                       const WindowOptions& options) {
  auto window = std::make_unique<GlfwWindow>(size, title, options);
  window->SetScreenHasDepth(options.depth);
  return window;
}

}  // namespace wlw::scene

#endif  // WLW_PLATFORM_GLFW
