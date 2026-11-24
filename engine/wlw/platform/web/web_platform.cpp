// Web platform layer (Emscripten): the page's <canvas id="canvas"> with a WebGL 2 context (= OpenGL ES 3.0),
// mouse / touch / keyboard from the browser's events, assets from the preloaded virtual file system, save
// files in IndexedDB (IDBFS mounted at /save).
//
// The browser owns the loop: nothing here may wait. RunWebApp returns to the browser, which calls one
// Application frame per display refresh (requestAnimationFrame) and presents the canvas when the frame
// returns, so SwapBuffers has nothing to do.
#ifdef WLW_PLATFORM_WEB

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <utility>

#include <emscripten.h>
#include <emscripten/html5.h>
#include <unistd.h>  // fsync

#include "wlw/app/application.h"
#include "wlw/platform/bridge.h"
#include "wlw/platform/platform.h"
#include "wlw/rendering/gl.h"
#include "wlw/scene/window.h"

namespace wlw::platform {
namespace {

constexpr const char* kCanvas = "#canvas";
constexpr const char* kSaveRoot = "/save";  // IDBFS: kept by the browser per site

bool g_alive = false;
int g_users = 0;  // Init() calls not yet matched by a Shutdown()
EMSCRIPTEN_WEBGL_CONTEXT_HANDLE g_context = 0;

std::unique_ptr<app::Application> (*g_create_app)() = nullptr;
app::Application* g_app = nullptr;  // on the heap for the page's lifetime: the browser unwound main()

// One Application frame per display refresh.
void WebFrame() {
  if (g_app == nullptr || g_app->Internal().Frame()) return;
  emscripten_cancel_main_loop();  // Quit(): the page stays, showing the last frame
  g_app->Internal().Shutdown();
}

}  // namespace

// The entry point's second half: the save files have arrived from IndexedDB (or never will), so the game
// can read them while it starts.
extern "C" EMSCRIPTEN_KEEPALIVE void wlw_web_start() {
  std::unique_ptr<app::Application> app = g_create_app ? g_create_app() : nullptr;
  if (!app) return;
  g_app = app.release();
  if (!g_app->Internal().Init()) {
    WLW_LOG(LogPlatform, Error, "The game couldn't start (no WebGL 2?)");
    EM_ASM({ if (Module.setStatus) Module.setStatus("This browser can't run the game (WebGL 2 is needed)."); });
    return;
  }
  EM_ASM({ if (Module.setStatus) Module.setStatus(""); });
  emscripten_set_main_loop(WebFrame, 0 /* requestAnimationFrame */, false);
}

void RunWebApp(std::unique_ptr<app::Application> (*create_app)()) {
  g_create_app = create_app;
  // Save data: IndexedDB behind a folder. Its files are loaded asynchronously, so the game starts from the
  // callback; autoPersist writes changes back on its own. Without IndexedDB (some private windows, storage
  // blocked for embedded pages) the folder still works for this visit.
  // clang-format off
  EM_ASM({
    var root = UTF8ToString($0);
    try {
      FS.mkdir(root);
      FS.mount(IDBFS, {autoPersist: true}, root);
      FS.syncfs(true, function(error) {
        if (error) console.warn("WLW: no saved data loaded", error);
        _wlw_web_start();
      });
    } catch (error) {
      console.warn("WLW: save data stays in memory", error);
      _wlw_web_start();
    }
  }, kSaveRoot);
  // clang-format on
}

bool Init() {
  if (g_alive) {
    ++g_users;
    return true;
  }
  g_alive = true;
  g_users = 1;
  return true;
}

void Shutdown() {
  if (!g_alive || --g_users > 0) return;
  if (g_context != 0) emscripten_webgl_destroy_context(g_context);
  g_context = 0;
  g_alive = false;
}

bool IsAlive() { return g_alive; }

bool HasCurrentGLContext() { return g_alive && emscripten_webgl_get_current_context() != 0; }

bool CurrentContextIsES() { return true; }

bool LoadGLFunctions() { return true; }  // linked directly, like libGLESv3 on Android

void* GetGLProcAddress(const char*) { return nullptr; }  // no extension functions by name (WebGL's are JS objects)

std::optional<std::vector<uint8_t>> ReadFile(const std::string& path) {
  std::ifstream file(path, std::ios::binary);  // the preloaded assets sit at the virtual root, the working directory
  if (!file) return std::nullopt;
  return std::vector<uint8_t>(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
}

std::string GetSaveDirectory(const std::string& app_name) {
  const std::filesystem::path folder = std::filesystem::path(kSaveRoot) / app_name;
  std::error_code error;
  std::filesystem::create_directories(folder, error);
  return std::filesystem::is_directory(folder, error) ? folder.string() : std::string();
}

bool WriteFileAtomic(const std::string& path, const std::vector<uint8_t>& data) {
  const std::string temp = path + ".tmp";
  FILE* file = std::fopen(temp.c_str(), "wb");
  if (!file) return false;
  bool ok = data.empty() || std::fwrite(data.data(), 1, data.size(), file) == data.size();
  ok = ok && std::fflush(file) == 0;
  ok = ok && fsync(fileno(file)) == 0;
  std::fclose(file);
  std::error_code error;
  if (ok) std::filesystem::rename(temp, path, error);  // the mount's autoPersist then stores it in IndexedDB
  if (!ok || error) {
    std::filesystem::remove(temp, error);
    return false;
  }
  return true;
}

std::optional<std::vector<uint8_t>> ReadLocalFile(const std::string& path) {
  std::ifstream file(path, std::ios::binary);
  if (!file) return std::nullopt;
  return std::vector<uint8_t>(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
}

SafeAreaInsets GetSafeAreaInsets() { return {}; }  // the page lays the canvas out inside the safe area

bool MoveToBackground() { return true; }  // Back with nothing left to close: stay (a page can't close itself)

void Vibrate(int milliseconds, float) {
  EM_ASM({ if (navigator.vibrate) { try { navigator.vibrate($0); } catch (e) {} } }, milliseconds);
}

void KeepScreenOn(bool) {}

bool ReduceMotion() {
  return EM_ASM_INT({ return window.matchMedia && window.matchMedia("(prefers-reduced-motion: reduce)").matches ? 1 : 0; }) != 0;
}

void PreciseSleep(double) {}  // nothing may wait on the browser's thread; requestAnimationFrame paces the frames

bool HasBridge() { return false; }
bool BridgeCall(const std::string&, const std::string&, const std::string&) { return false; }

void WriteLog(const LogRecord& record) {
  const std::string line = FormatLogRecord(record);
  if (record.level >= LogLevel::Error) {
    emscripten_console_error(line.c_str());
  } else if (record.level >= LogLevel::Warning) {
    emscripten_console_warn(line.c_str());
  } else {
    emscripten_console_log(line.c_str());
  }
}

bool IsDebuggerAttached() { return false; }
void BreakIntoDebugger() {}

}  // namespace wlw::platform

namespace wlw::scene {
namespace {

// Phones report 3+ device pixels per CSS pixel: more than the picture needs, and 2.25x the pixels of 2.
constexpr double kMaxPixelRatio = 2.0;

input::Key MapKey(const char* code) {
  struct Entry {
    const char* code;
    input::Key key;
  };
  static constexpr Entry kKeys[] = {
      {"ArrowUp", input::Key::Up},         {"ArrowDown", input::Key::Down},       {"ArrowLeft", input::Key::Left},
      {"ArrowRight", input::Key::Right},   {"Space", input::Key::Space},          {"Enter", input::Key::Enter},
      {"Escape", input::Key::Escape},      {"KeyW", input::Key::W},               {"KeyA", input::Key::A},
      {"KeyS", input::Key::S},             {"KeyD", input::Key::D},               {"KeyI", input::Key::I},
      {"KeyP", input::Key::P},             {"F1", input::Key::F1},                {"Tab", input::Key::Tab},
      {"Backspace", input::Key::Backspace}, {"Delete", input::Key::Delete},       {"Home", input::Key::Home},
      {"End", input::Key::End},            {"PageUp", input::Key::PageUp},        {"PageDown", input::Key::PageDown},
      {"ControlLeft", input::Key::Control}, {"ControlRight", input::Key::Control}, {"ShiftLeft", input::Key::Shift},
      {"ShiftRight", input::Key::Shift},   {"Backquote", input::Key::Grave},
  };
  for (const Entry& entry : kKeys) {
    if (std::strcmp(code, entry.code) == 0) return entry.key;
  }
  return input::Key::Unknown;
}

class WebWindow : public Window {
 public:
  WebWindow(const Vec2& size, const WindowOptions& options) : options_(options) { size_ = size; }

  ~WebWindow() override {
    if (!listening_) return;
    emscripten_set_mousedown_callback(platform::kCanvas, nullptr, false, nullptr);
    emscripten_set_mousemove_callback(EMSCRIPTEN_EVENT_TARGET_WINDOW, nullptr, false, nullptr);
    emscripten_set_mouseup_callback(EMSCRIPTEN_EVENT_TARGET_WINDOW, nullptr, false, nullptr);
    emscripten_set_touchstart_callback(platform::kCanvas, nullptr, false, nullptr);
    emscripten_set_touchmove_callback(platform::kCanvas, nullptr, false, nullptr);
    emscripten_set_touchend_callback(platform::kCanvas, nullptr, false, nullptr);
    emscripten_set_touchcancel_callback(platform::kCanvas, nullptr, false, nullptr);
    emscripten_set_keydown_callback(EMSCRIPTEN_EVENT_TARGET_WINDOW, nullptr, false, nullptr);
    emscripten_set_keyup_callback(EMSCRIPTEN_EVENT_TARGET_WINDOW, nullptr, false, nullptr);
    emscripten_set_visibilitychange_callback(nullptr, false, nullptr);
  }

  bool Initialize() override {
    if (platform::g_context != 0) return true;
    EmscriptenWebGLContextAttributes attributes;
    emscripten_webgl_init_context_attributes(&attributes);
    attributes.majorVersion = 2;  // WebGL 2 = OpenGL ES 3.0
    attributes.minorVersion = 0;
    attributes.alpha = false;  // the page never shows through the picture
    attributes.depth = options_.depth;
    attributes.stencil = options_.depth;
    attributes.antialias = false;
    attributes.powerPreference = EM_WEBGL_POWER_PREFERENCE_HIGH_PERFORMANCE;
    attributes.enableExtensionsByDefault = true;  // float render targets (EXT_color_buffer_float) among them
    platform::g_context = emscripten_webgl_create_context(platform::kCanvas, &attributes);
    if (platform::g_context <= 0) {
      WLW_LOG(LogPlatform, Error, "No WebGL 2 context on %s (error %d)", platform::kCanvas,
              static_cast<int>(platform::g_context));
      platform::g_context = 0;
      return false;
    }
    emscripten_webgl_make_context_current(platform::g_context);
    UpdateSize();

    emscripten_set_mousedown_callback(platform::kCanvas, this, false, &WebWindow::OnMouse);
    // Moves and the release on the whole window: a drag that leaves the canvas keeps going and ends.
    emscripten_set_mousemove_callback(EMSCRIPTEN_EVENT_TARGET_WINDOW, this, false, &WebWindow::OnMouse);
    emscripten_set_mouseup_callback(EMSCRIPTEN_EVENT_TARGET_WINDOW, this, false, &WebWindow::OnMouse);
    emscripten_set_touchstart_callback(platform::kCanvas, this, false, &WebWindow::OnTouch);
    emscripten_set_touchmove_callback(platform::kCanvas, this, false, &WebWindow::OnTouch);
    emscripten_set_touchend_callback(platform::kCanvas, this, false, &WebWindow::OnTouch);
    emscripten_set_touchcancel_callback(platform::kCanvas, this, false, &WebWindow::OnTouch);
    emscripten_set_keydown_callback(EMSCRIPTEN_EVENT_TARGET_WINDOW, this, false, &WebWindow::OnKey);
    emscripten_set_keyup_callback(EMSCRIPTEN_EVENT_TARGET_WINDOW, this, false, &WebWindow::OnKey);
    emscripten_set_visibilitychange_callback(this, false, &WebWindow::OnVisibility);
    listening_ = true;
    return true;
  }

  void MakeContextCurrent() override {
    if (platform::g_context != 0) emscripten_webgl_make_context_current(platform::g_context);
  }

  // The browser delivered the events between frames (the callbacks below); what's left is the canvas's
  // size: the page lays it out in CSS, the drawing buffer follows.
  void ProcessEvents() override { UpdateSize(); }

  void SwapBuffers() override {}  // the browser presents the canvas when the frame's callback returns

  bool ShouldClose() const override { return false; }  // a tab closes without telling us
  void RequestClose() override {}
  void SetTitle(const std::string&) override {}  // the page has its own title

  bool ConsumePaused() override { return std::exchange(paused_, false); }
  bool ConsumeResumed() override { return std::exchange(resumed_, false); }

  std::string PacingMethod() const override { return "requestAnimationFrame"; }

 private:
  static WebWindow* From(void* user_data) { return static_cast<WebWindow*>(user_data); }

  void UpdateSize() {
    double css_width = 0.0, css_height = 0.0;
    if (emscripten_get_element_css_size(platform::kCanvas, &css_width, &css_height) != EMSCRIPTEN_RESULT_SUCCESS) return;
    if (css_width < 1.0 || css_height < 1.0) return;  // not laid out (hidden): keep the last size
    const double ratio = std::min(emscripten_get_device_pixel_ratio(), kMaxPixelRatio);
    const int width = std::max(1, static_cast<int>(css_width * ratio + 0.5));
    const int height = std::max(1, static_cast<int>(css_height * ratio + 0.5));
    if (width != buffer_width_ || height != buffer_height_) {
      emscripten_set_canvas_element_size(platform::kCanvas, width, height);
      buffer_width_ = width;
      buffer_height_ = height;
      size_ = {static_cast<float>(width), static_cast<float>(height)};
    }
    // Pointer positions come in CSS pixels from the canvas's corner.
    input_.Internal().SetSurfaceSize(static_cast<float>(css_width), static_cast<float>(css_height));
  }

  // A page position (CSS pixels from the window's corner) as a canvas one.
  static Vec2 ToCanvas(double client_x, double client_y) {
    const double left = EM_ASM_DOUBLE({ return Module.canvas.getBoundingClientRect().left; });
    const double top = EM_ASM_DOUBLE({ return Module.canvas.getBoundingClientRect().top; });
    return {static_cast<float>(client_x - left), static_cast<float>(client_y - top)};
  }

  static EM_BOOL OnMouse(int type, const EmscriptenMouseEvent* event, void* user_data) {
    WebWindow* self = From(user_data);
    const Vec2 at = ToCanvas(event->clientX, event->clientY);
    if (type == EMSCRIPTEN_EVENT_MOUSEMOVE) {
      self->input_.Internal().OnPointerMove(at.x, at.y);
      return EM_FALSE;
    }
    if (event->button != 0) return EM_FALSE;  // the left button only
    if (type == EMSCRIPTEN_EVENT_MOUSEDOWN) {
      self->mouse_down_ = true;
      self->input_.Internal().OnPointerDown(at.x, at.y);
    } else if (self->mouse_down_) {  // a release that began on the canvas
      self->mouse_down_ = false;
      self->input_.Internal().OnPointerUp(at.x, at.y);
    }
    return EM_FALSE;  // the browser's own handling stays (focus, miniaudio's "first click" audio unlock)
  }

  // The first finger is the pointer (Input has no multi-touch yet). Returning true stops the browser from
  // scrolling / zooming the page and from sending the same touch again as mouse events.
  static EM_BOOL OnTouch(int type, const EmscriptenTouchEvent* event, void* user_data) {
    WebWindow* self = From(user_data);
    for (int i = 0; i < event->numTouches; ++i) {
      const EmscriptenTouchPoint& touch = event->touches[i];
      if (!touch.isChanged) continue;
      const Vec2 at = ToCanvas(touch.clientX, touch.clientY);
      if (type == EMSCRIPTEN_EVENT_TOUCHSTART) {
        if (self->finger_ >= 0) continue;
        self->finger_ = touch.identifier;
        self->input_.Internal().OnPointerDown(at.x, at.y);
      } else if (touch.identifier == self->finger_) {
        if (type == EMSCRIPTEN_EVENT_TOUCHMOVE) {
          self->input_.Internal().OnPointerMove(at.x, at.y);
        } else {  // end or cancel
          self->finger_ = -1;
          self->input_.Internal().OnPointerUp(at.x, at.y);
        }
      }
    }
    return EM_TRUE;
  }

  static EM_BOOL OnKey(int type, const EmscriptenKeyboardEvent* event, void* user_data) {
    WebWindow* self = From(user_data);
    const bool down = type == EMSCRIPTEN_EVENT_KEYDOWN;
    if (down && event->key[0] != '\0' && event->key[1] == '\0' && !event->ctrlKey && !event->metaKey) {
      self->input_.Internal().OnText(static_cast<unsigned char>(event->key[0]));  // typed ASCII (debug console)
    }
    const input::Key key = MapKey(event->code);
    if (key == input::Key::Unknown) return EM_FALSE;
    if (!(down && event->repeat)) self->input_.Internal().OnKey(key, down);
    // Keys the page would act on (scrolling with the arrows / space, Tab leaving the canvas).
    return key == input::Key::Up || key == input::Key::Down || key == input::Key::Left || key == input::Key::Right ||
           key == input::Key::Space || key == input::Key::Tab;
  }

  // Tab hidden (switched away, phone locked) = the app's pause; shown again = its resume. A hidden page gets
  // no more frames, so the pause's frame runs right here: OnAppPaused saves before the tab can be closed.
  static EM_BOOL OnVisibility(int, const EmscriptenVisibilityChangeEvent* event, void* user_data) {
    WebWindow* self = From(user_data);
    if (event->hidden) {
      self->paused_ = true;
      if (self->finger_ >= 0 || self->mouse_down_) {  // a drag in progress ends where it was
        const Vec2 at = self->input_.PointerPosition();
        self->input_.Internal().OnPointerUp(at.x, at.y);
        self->finger_ = -1;
        self->mouse_down_ = false;
      }
      platform::WebFrame();
    } else {
      self->resumed_ = true;
    }
    return EM_FALSE;
  }

  WindowOptions options_;
  int buffer_width_ = 0;
  int buffer_height_ = 0;
  int finger_ = -1;  // the touch that is the pointer, -1: none
  bool mouse_down_ = false;
  bool paused_ = false;
  bool resumed_ = false;
  bool listening_ = false;
};

}  // namespace

std::unique_ptr<Window> Window::Create(const Vec2& size, const std::string& /*title*/, const WindowOptions& options) {
  auto window = std::make_unique<WebWindow>(size, options);
  window->Internal().SetScreenHasDepth(options.depth);
  return window;
}

}  // namespace wlw::scene

#endif  // WLW_PLATFORM_WEB
