// Desktop platform layer (Windows, macOS, Linux): GLFW for windows, GL contexts and input,
// glad for loading OpenGL 3.3 core functions.
#ifdef WLW_PLATFORM_GLFW

#include <array>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>

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
int g_users = 0;  // Init() calls not yet matched by a Shutdown()

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
  if (g_alive) {
    ++g_users;
    return true;
  }
  glfwSetErrorCallback(GlfwErrorCallback);
  if (!glfwInit()) return false;
  g_alive = true;
  g_users = 1;

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
  if (!g_alive || --g_users > 0) return;
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

bool MoveToBackground() { return false; }  // no such thing on desktop: the app quits instead

void Vibrate(int, float) {}  // desktops don't vibrate

void KeepScreenOn(bool) {}  // a desktop's screensaver is the user's business

bool ReduceMotion() {
#if defined(_WIN32)
  // Settings > Accessibility > Visual effects > "Animation effects" (read each call: a cheap user-mode query).
  BOOL animations = TRUE;
  return SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION, 0, &animations, 0) && !animations;
#else
  return false;
#endif
}

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
  using input::Key;
  if (key >= GLFW_KEY_A && key <= GLFW_KEY_Z) return static_cast<Key>(static_cast<int>(Key::A) + (key - GLFW_KEY_A));
  if (key >= GLFW_KEY_0 && key <= GLFW_KEY_9) {
    return static_cast<Key>(static_cast<int>(Key::Digit0) + (key - GLFW_KEY_0));
  }
  if (key >= GLFW_KEY_F1 && key <= GLFW_KEY_F12)
    return static_cast<Key>(static_cast<int>(Key::F1) + (key - GLFW_KEY_F1));
  if (key >= GLFW_KEY_KP_0 && key <= GLFW_KEY_KP_9) {
    return static_cast<Key>(static_cast<int>(Key::Numpad0) + (key - GLFW_KEY_KP_0));
  }
  switch (key) {
    case GLFW_KEY_GRAVE_ACCENT: return Key::Grave;
    case GLFW_KEY_MINUS: return Key::Minus;
    case GLFW_KEY_EQUAL: return Key::Equal;
    case GLFW_KEY_LEFT_BRACKET: return Key::LeftBracket;
    case GLFW_KEY_RIGHT_BRACKET: return Key::RightBracket;
    case GLFW_KEY_BACKSLASH: return Key::Backslash;
    case GLFW_KEY_SEMICOLON: return Key::Semicolon;
    case GLFW_KEY_APOSTROPHE: return Key::Apostrophe;
    case GLFW_KEY_COMMA: return Key::Comma;
    case GLFW_KEY_PERIOD: return Key::Period;
    case GLFW_KEY_SLASH: return Key::Slash;
    case GLFW_KEY_WORLD_1:
    case GLFW_KEY_WORLD_2: return Key::IntlBackslash;  // the ISO key (GLFW names it by platform)
    case GLFW_KEY_SPACE: return Key::Space;
    case GLFW_KEY_ENTER: return Key::Enter;
    case GLFW_KEY_ESCAPE: return Key::Escape;
    case GLFW_KEY_TAB: return Key::Tab;
    case GLFW_KEY_BACKSPACE: return Key::Backspace;
    case GLFW_KEY_INSERT: return Key::Insert;
    case GLFW_KEY_DELETE: return Key::Delete;
    case GLFW_KEY_HOME: return Key::Home;
    case GLFW_KEY_END: return Key::End;
    case GLFW_KEY_PAGE_UP: return Key::PageUp;
    case GLFW_KEY_PAGE_DOWN: return Key::PageDown;
    case GLFW_KEY_UP: return Key::Up;
    case GLFW_KEY_DOWN: return Key::Down;
    case GLFW_KEY_LEFT: return Key::Left;
    case GLFW_KEY_RIGHT: return Key::Right;
    case GLFW_KEY_CAPS_LOCK: return Key::CapsLock;
    case GLFW_KEY_SCROLL_LOCK: return Key::ScrollLock;
    case GLFW_KEY_NUM_LOCK: return Key::NumLock;
    case GLFW_KEY_PRINT_SCREEN: return Key::PrintScreen;
    case GLFW_KEY_PAUSE: return Key::Pause;
    case GLFW_KEY_MENU: return Key::Menu;
    case GLFW_KEY_KP_DECIMAL: return Key::NumpadDecimal;
    case GLFW_KEY_KP_DIVIDE: return Key::NumpadDivide;
    case GLFW_KEY_KP_MULTIPLY: return Key::NumpadMultiply;
    case GLFW_KEY_KP_SUBTRACT: return Key::NumpadSubtract;
    case GLFW_KEY_KP_ADD: return Key::NumpadAdd;
    case GLFW_KEY_KP_ENTER: return Key::NumpadEnter;
    case GLFW_KEY_KP_EQUAL: return Key::NumpadEqual;
    case GLFW_KEY_LEFT_SHIFT: return Key::LeftShift;
    case GLFW_KEY_RIGHT_SHIFT: return Key::RightShift;
    case GLFW_KEY_LEFT_CONTROL: return Key::LeftControl;
    case GLFW_KEY_RIGHT_CONTROL: return Key::RightControl;
    case GLFW_KEY_LEFT_ALT: return Key::LeftAlt;
    case GLFW_KEY_RIGHT_ALT: return Key::RightAlt;
    case GLFW_KEY_LEFT_SUPER: return Key::LeftSuper;
    case GLFW_KEY_RIGHT_SUPER: return Key::RightSuper;
    default: return Key::Unknown;
  }
}

// The key a printable key's label names on the current layout (D18: shortcuts follow the label), like Dear ImGui's
// GLFW backend: GLFW names the character the key types; letters, digits and the US punctuation map back to keys.
// Unknown (= the key's position) for the rest: numpad, non-printing keys, other alphabets.
input::Key MapLayoutKey(int key, int scancode) {
  if (key < GLFW_KEY_SPACE || key > GLFW_KEY_WORLD_2 || key == GLFW_KEY_SPACE) return input::Key::Unknown;
  GLFWerrorfun previous = glfwSetErrorCallback(nullptr);  // keys without a name report an error
  const char* name = glfwGetKeyName(key, scancode);
  glfwSetErrorCallback(previous);
  if (name == nullptr || name[0] == '\0' || name[1] != '\0') return input::Key::Unknown;  // one ASCII character
  return input::KeyFromCharacter(static_cast<unsigned char>(name[0]));
}

// GLFW's buttons 0-4 in MouseButton's order (left, right, middle, back = 4th, forward = 5th).
std::optional<input::MouseButton> MapMouseButton(int button) {
  if (button < GLFW_MOUSE_BUTTON_1 || button > GLFW_MOUSE_BUTTON_5) return std::nullopt;
  return static_cast<input::MouseButton>(button - GLFW_MOUSE_BUTTON_1);
}

int StandardCursorShape(Cursor cursor) {
  switch (cursor) {
    case Cursor::TextInput: return GLFW_IBEAM_CURSOR;
    case Cursor::Hand: return GLFW_POINTING_HAND_CURSOR;
    case Cursor::Crosshair: return GLFW_CROSSHAIR_CURSOR;
    case Cursor::ResizeEW: return GLFW_RESIZE_EW_CURSOR;
    case Cursor::ResizeNS: return GLFW_RESIZE_NS_CURSOR;
    case Cursor::ResizeNWSE: return GLFW_RESIZE_NWSE_CURSOR;
    case Cursor::ResizeNESW: return GLFW_RESIZE_NESW_CURSOR;
    case Cursor::ResizeAll: return GLFW_RESIZE_ALL_CURSOR;
    case Cursor::NotAllowed: return GLFW_NOT_ALLOWED_CURSOR;
    case Cursor::Arrow:
    case Cursor::Hidden: break;
  }
  return GLFW_ARROW_CURSOR;
}

class GlfwWindow : public Window {
 public:
  GlfwWindow(const Vec2& size, const std::string& title, const WindowOptions& options)
      : title_(title), options_(options) {
    size_ = size;
  }

  ~GlfwWindow() override {
    // If the platform was already shut down, glfwTerminate() destroyed this window for us.
    if (window_ != nullptr && platform::IsAlive()) {
      glfwDestroyWindow(window_);
      for (GLFWcursor* cursor : cursors_) {
        if (cursor != nullptr) glfwDestroyCursor(cursor);
      }
    }
    window_ = nullptr;
  }

  bool Initialize() override {
    if (window_ != nullptr) {
      WLW_LOG(LogPlatform, Warning, "Window '%s' is already initialized", title_.c_str());
      return true;
    }
    glfwWindowHint(GLFW_VISIBLE, options_.visible ? GLFW_TRUE : GLFW_FALSE);
    glfwWindowHint(GLFW_DEPTH_BITS, options_.depth ? 24 : 0);  // no depth: no stencil either
    glfwWindowHint(GLFW_STENCIL_BITS, options_.depth ? 8 : 0);
    if (options_.gles) {  // OpenGL ES 3.0 (the profile hint doesn't apply to ES)
      glfwWindowHint(GLFW_CLIENT_API, GLFW_OPENGL_ES_API);
      glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);
    }
    window_ = glfwCreateWindow((int)size_.x, (int)size_.y, title_.c_str(), nullptr, nullptr);
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
    glfwSetScrollCallback(window_, &GlfwWindow::OnScroll);
    glfwSetWindowFocusCallback(window_, &GlfwWindow::OnFocus);
    glfwSetFramebufferSizeCallback(window_, &GlfwWindow::OnFramebufferSize);
    glfwSetWindowPosCallback(window_, &GlfwWindow::OnWindowPos);

    int fb_w = 0, fb_h = 0;
    glfwGetFramebufferSize(window_, &fb_w, &fb_h);  // pixels (can differ from size_ on HiDPI)
    size_ = {(float)fb_w, (float)fb_h};
    UpdateInputSurface();
    focused_ = glfwGetWindowAttrib(window_, GLFW_FOCUSED) == GLFW_TRUE;
    ApplyCursor();

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

  static void OnKey(GLFWwindow* w, int key, int scancode, int action, int /*mods*/) {
    if (action == GLFW_REPEAT) return;
    const bool down = action == GLFW_PRESS;
    From(w)->input_.Internal().OnKey(MapKey(key), down, down ? MapLayoutKey(key, scancode) : input::Key::Unknown);
  }

  static void OnChar(GLFWwindow* w, unsigned int codepoint) { From(w)->input_.Internal().OnText(codepoint); }

  static void OnMouseButton(GLFWwindow* w, int button, int action, int /*mods*/) {
    if (const std::optional<input::MouseButton> mapped = MapMouseButton(button)) {
      From(w)->input_.Internal().OnMouseButton(*mapped, action == GLFW_PRESS);
    }
    if (button != GLFW_MOUSE_BUTTON_LEFT) return;  // the pointer = the left button
    double x = 0, y = 0;
    glfwGetCursorPos(w, &x, &y);
    if (action == GLFW_PRESS)
      From(w)->input_.Internal().OnPointerDown((float)x, (float)y);
    else
      From(w)->input_.Internal().OnPointerUp((float)x, (float)y);
  }

  static void OnCursorPos(GLFWwindow* w, double x, double y) {
    From(w)->input_.Internal().OnPointerMove((float)x, (float)y);
  }

  // GLFW: +y = away from the player, +x = left (Windows' tilt right is negative): turned to +x = right.
  static void OnScroll(GLFWwindow* w, double x, double y) {
    From(w)->input_.Internal().OnScroll(static_cast<float>(-x), static_cast<float>(y));
  }

  // GLFW releases held keys and buttons itself when focus goes (through the callbacks above).
  static void OnFocus(GLFWwindow* w, int focused) { From(w)->focused_ = focused == GLFW_TRUE; }

  // Shape, hidden or locked (GLFW_CURSOR_DISABLED: hidden, held in the window, unbounded positions; raw motion where
  // the OS has it). GLFW itself frees a locked cursor while the window is unfocused.
  void ApplyCursor() override {
    if (window_ == nullptr) return;
    const Cursor shape = EffectiveCursor();
    const int mode = cursor_locked_            ? GLFW_CURSOR_DISABLED
                     : shape == Cursor::Hidden ? GLFW_CURSOR_HIDDEN
                                               : GLFW_CURSOR_NORMAL;
    const int current = glfwGetInputMode(window_, GLFW_CURSOR);
    if (mode != current) {
      if ((mode == GLFW_CURSOR_DISABLED) != (current == GLFW_CURSOR_DISABLED)) {
        input_.Internal().ForgetPointerPosition();  // positions jump between the real and the unbounded cursor
      }
      if (glfwRawMouseMotionSupported()) {
        glfwSetInputMode(window_, GLFW_RAW_MOUSE_MOTION, mode == GLFW_CURSOR_DISABLED ? GLFW_TRUE : GLFW_FALSE);
      }
      glfwSetInputMode(window_, GLFW_CURSOR, mode);
    }
    if (mode == GLFW_CURSOR_NORMAL && shape != applied_shape_) {
      glfwSetCursor(window_, StandardCursor(shape));
      applied_shape_ = shape;
    }
  }

  // The system cursor for a shape, made once (null = the arrow: GLFW's default, also when the system lacks it).
  GLFWcursor* StandardCursor(Cursor shape) {
    if (shape == Cursor::Arrow) return nullptr;
    const auto index = static_cast<size_t>(shape);
    if (!cursor_tried_[index]) {
      cursor_tried_[index] = true;
      GLFWerrorfun previous = glfwSetErrorCallback(nullptr);  // GLFW_CURSOR_UNAVAILABLE: logged below instead
      cursors_[index] = glfwCreateStandardCursor(StandardCursorShape(shape));
      glfwSetErrorCallback(previous);
      if (cursors_[index] == nullptr) {
        WLW_LOG(LogPlatform, Info, "Cursor shape %d unavailable here: the arrow instead", static_cast<int>(shape));
      }
    }
    return cursors_[index];
  }

  static void OnFramebufferSize(GLFWwindow* w, int width, int height) {
    GlfwWindow* self = From(w);
    self->size_ = {(float)width, (float)height};
    self->UpdateInputSurface();
  }

  void UpdateInputSurface() {
    int w = 0, h = 0;
    glfwGetWindowSize(window_, &w, &h);  // cursor coordinates are in window units, not pixels
    input_.Internal().SetSurfaceSize((float)w, (float)h);
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
  static constexpr size_t kCursorCount = static_cast<size_t>(Cursor::Hidden) + 1;
  std::array<GLFWcursor*, kCursorCount> cursors_{};
  std::array<bool, kCursorCount> cursor_tried_{};
  Cursor applied_shape_ = Cursor::Arrow;
};

}  // namespace

std::unique_ptr<Window> Window::Create(const Vec2& size, const std::string& title, const WindowOptions& options) {
  auto window = std::make_unique<GlfwWindow>(size, title, options);
  window->Internal().SetScreenHasDepth(options.depth);
  return window;
}

}  // namespace wlw::scene

#endif  // WLW_PLATFORM_GLFW
