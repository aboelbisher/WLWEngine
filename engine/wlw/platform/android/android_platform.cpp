// Android platform layer: NativeActivity (via android_native_app_glue) + EGL + OpenGL ES 3.0.
//
// How an Android app runs here:
//   - Android starts NativeActivity, which loads libwlw_snake.so and calls android_main() on a
//     dedicated thread (see app/entry_point.h -> RunAndroidApp below).
//   - The OS talks to us through events polled from a looper: lifecycle commands (window
//     created/destroyed, paused/resumed...) and input (touch, keys).
//   - The drawing surface comes and goes: when the user switches apps, Android destroys our
//     window. We keep the EGL *context* (so textures/meshes survive) and only recreate the
//     *surface* when the window comes back.
#ifdef WLW_PLATFORM_ANDROID

#include <algorithm>
#include <memory>

#include <EGL/egl.h>
#include <GLES3/gl3.h>
#include <jni.h>
#include <android/asset_manager.h>
#include <android/configuration.h>
#include <android/log.h>
#include <android/native_window.h>
#include <android_native_app_glue.h>

#include "wlw/app/application.h"
#include "wlw/platform/platform.h"
#include "wlw/scene/window.h"

#ifndef EGL_OPENGL_ES3_BIT
#define EGL_OPENGL_ES3_BIT 0x00000040
#endif

namespace wlw::platform {
namespace {

android_app* g_app = nullptr;
bool g_alive = false;
EGLDisplay g_display = EGL_NO_DISPLAY;
SafeAreaInsets g_insets;      // cached; refreshed when the window changes
bool g_insets_valid = false;

// Processes pending OS events. timeout_ms = -1 blocks until at least one event arrives.
void PollEvents(int timeout_ms) {
  int events = 0;
  android_poll_source* source = nullptr;
  while (ALooper_pollOnce(timeout_ms, nullptr, &events, reinterpret_cast<void**>(&source)) >= 0) {
    if (source) source->process(g_app, source);  // dispatches to OnAppCmd / OnInputEvent
    if (g_app->destroyRequested) return;
    timeout_ms = 0;  // after the first event, only drain what's already pending
  }
}

input::Key MapKey(int32_t keycode) {
  switch (keycode) {
    case AKEYCODE_DPAD_UP: return input::Key::Up;  // the emulator's arrow keys arrive as D-pad
    case AKEYCODE_DPAD_DOWN: return input::Key::Down;
    case AKEYCODE_DPAD_LEFT: return input::Key::Left;
    case AKEYCODE_DPAD_RIGHT: return input::Key::Right;
    case AKEYCODE_SPACE: return input::Key::Space;
    case AKEYCODE_ENTER:
    case AKEYCODE_DPAD_CENTER: return input::Key::Enter;
    case AKEYCODE_ESCAPE: return input::Key::Escape;
    case AKEYCODE_W: return input::Key::W;
    case AKEYCODE_A: return input::Key::A;
    case AKEYCODE_S: return input::Key::S;
    case AKEYCODE_D: return input::Key::D;
    case AKEYCODE_I: return input::Key::I;
    case AKEYCODE_P: return input::Key::P;
    case AKEYCODE_F1: return input::Key::F1;
    case AKEYCODE_BACK: return input::Key::Back;  // handled by the game (Application), not Android
    default: return input::Key::Unknown;
  }
}

class AndroidWindow;
AndroidWindow* g_window = nullptr;  // Android apps have exactly one window

class AndroidWindow : public scene::Window {
 public:
  AndroidWindow() { g_window = this; }

  ~AndroidWindow() override {
    DestroySurface();
    DestroyContext();
    if (g_window == this) g_window = nullptr;
  }

  bool Initialize(scene::Window* share_with) override {
    if (share_with) WLW_LOG_WARN("Multiple windows aren't supported on Android");
    input_.SetTouchScreen(true);  // the pointer is a finger: no hover, coarse aim
    // The native window arrives asynchronously (APP_CMD_INIT_WINDOW): wait for it.
    while (g_app->window == nullptr && !g_app->destroyRequested) PollEvents(-1);
    if (g_app->destroyRequested) return false;
    return CreateContext() && CreateSurface();
  }

  void MakeContextCurrent() override {
    if (surface_ != EGL_NO_SURFACE) eglMakeCurrent(g_display, surface_, surface_, context_);
  }

  void ProcessEvents() override {
    PollEvents(0);
    // While in the background (or without a surface) there's nothing to draw: sleep in the
    // event loop instead of spinning. The game loop simply stops until we're back.
    while (!CanRender() && !g_app->destroyRequested && !close_requested_) {
      was_suspended_ = true;
      PollEvents(-1);
    }
  }

  void SwapBuffers() override {
    if (surface_ == EGL_NO_SURFACE) return;
    if (eglSwapBuffers(g_display, surface_) == EGL_TRUE) return;
    const EGLint error = eglGetError();
    if (error == EGL_BAD_SURFACE || error == EGL_BAD_NATIVE_WINDOW) {
      DestroySurface();  // recreated on the next APP_CMD_INIT_WINDOW
    } else {
      WLW_LOG_ERROR("eglSwapBuffers failed: 0x%x", error);
    }
  }

  bool ShouldClose() const override { return close_requested_ || g_app->destroyRequested; }
  void RequestClose() override { close_requested_ = true; }
  void SetTitle(const std::string&) override {}  // no title bar on Android

  bool ConsumePaused() override {
    const bool paused = paused_event_;
    paused_event_ = false;
    return paused;
  }

  bool ConsumeResumed() override {
    const bool resumed = was_suspended_;
    was_suspended_ = false;
    return resumed;
  }

  // ---- Called from the event handlers ----
  void HandleCommand(int32_t cmd) {
    switch (cmd) {
      case APP_CMD_INIT_WINDOW:  // window (re)created, e.g. after returning to the app
        if (context_ != EGL_NO_CONTEXT) CreateSurface();
        break;
      case APP_CMD_TERM_WINDOW:  // window about to be destroyed: stop using it
        DestroySurface();
        break;
      case APP_CMD_PAUSE:
        resumed_ = false;
        paused_event_ = true;  // the game should pause itself (phone call, app switch...)
        break;
      case APP_CMD_RESUME:
        resumed_ = true;
        break;
      case APP_CMD_WINDOW_RESIZED:
      case APP_CMD_CONFIG_CHANGED:
        UpdateSize();
        break;
      default:
        break;
    }
  }

  int32_t HandleInput(AInputEvent* event) {
    const int32_t type = AInputEvent_getType(event);
    if (type == AINPUT_EVENT_TYPE_MOTION) {
      const int32_t action = AMotionEvent_getAction(event) & AMOTION_EVENT_ACTION_MASK;
      const float x = AMotionEvent_getX(event, 0);  // first finger only
      const float y = AMotionEvent_getY(event, 0);
      switch (action) {
        case AMOTION_EVENT_ACTION_DOWN: input_.OnPointerDown(x, y); break;
        case AMOTION_EVENT_ACTION_MOVE: input_.OnPointerMove(x, y); break;
        case AMOTION_EVENT_ACTION_UP:
        case AMOTION_EVENT_ACTION_CANCEL: input_.OnPointerUp(x, y); break;
        default: break;
      }
      return 1;
    }
    if (type == AINPUT_EVENT_TYPE_KEY) {
      const int32_t keycode = AKeyEvent_getKeyCode(event);
      const input::Key key = MapKey(keycode);
      if (key == input::Key::Unknown) return 0;
      const int32_t action = AKeyEvent_getAction(event);
      if (action == AKEY_EVENT_ACTION_DOWN) input_.OnKey(key, true);
      if (action == AKEY_EVENT_ACTION_UP) input_.OnKey(key, false);
      return 1;
    }
    return 0;
  }

  void DestroySurface() {
    if (surface_ == EGL_NO_SURFACE) return;
    eglMakeCurrent(g_display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    eglDestroySurface(g_display, surface_);
    surface_ = EGL_NO_SURFACE;
  }

  void DestroyContext() {
    if (context_ == EGL_NO_CONTEXT) return;
    eglMakeCurrent(g_display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    eglDestroyContext(g_display, context_);
    context_ = EGL_NO_CONTEXT;
  }

 private:
  bool CanRender() const { return resumed_ && surface_ != EGL_NO_SURFACE; }

  bool CreateContext() {
    if (context_ != EGL_NO_CONTEXT) return true;
    if (g_display == EGL_NO_DISPLAY) {
      g_display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
      if (g_display == EGL_NO_DISPLAY || eglInitialize(g_display, nullptr, nullptr) != EGL_TRUE) {
        WLW_LOG_ERROR("eglInitialize failed: 0x%x", eglGetError());
        return false;
      }
    }
    // An RGB888 window surface with a 24-bit depth buffer that supports OpenGL ES 3.
    const EGLint config_attribs[] = {EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT,
                                     EGL_SURFACE_TYPE,    EGL_WINDOW_BIT,
                                     EGL_RED_SIZE,        8,
                                     EGL_GREEN_SIZE,      8,
                                     EGL_BLUE_SIZE,       8,
                                     EGL_DEPTH_SIZE,      24,
                                     EGL_NONE};
    EGLint num_configs = 0;
    if (eglChooseConfig(g_display, config_attribs, &config_, 1, &num_configs) != EGL_TRUE || num_configs == 0) {
      WLW_LOG_ERROR("No EGL config with OpenGL ES 3 + 24-bit depth");
      return false;
    }
    const EGLint context_attribs[] = {EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE};
    context_ = eglCreateContext(g_display, config_, EGL_NO_CONTEXT, context_attribs);
    if (context_ == EGL_NO_CONTEXT) {
      WLW_LOG_ERROR("eglCreateContext failed: 0x%x", eglGetError());
      return false;
    }
    return true;
  }

  bool CreateSurface() {
    if (surface_ != EGL_NO_SURFACE || g_app->window == nullptr) return surface_ != EGL_NO_SURFACE;
    EGLint format = 0;
    eglGetConfigAttrib(g_display, config_, EGL_NATIVE_VISUAL_ID, &format);
    ANativeWindow_setBuffersGeometry(g_app->window, 0, 0, format);
    surface_ = eglCreateWindowSurface(g_display, config_, g_app->window, nullptr);
    if (surface_ == EGL_NO_SURFACE) {
      WLW_LOG_ERROR("eglCreateWindowSurface failed: 0x%x", eglGetError());
      return false;
    }
    eglMakeCurrent(g_display, surface_, surface_, context_);
    eglSwapInterval(g_display, 1);  // vsync
    UpdateSize();
    return true;
  }

  void UpdateSize() {
    if (surface_ == EGL_NO_SURFACE) return;
    EGLint width = 0, height = 0;
    eglQuerySurface(g_display, surface_, EGL_WIDTH, &width);
    eglQuerySurface(g_display, surface_, EGL_HEIGHT, &height);
    size_ = {(float)width, (float)height};
    g_insets_valid = false;  // size or orientation changed: ask for the insets again
    input_.SetSurfaceSize((float)width, (float)height);
  }

  EGLConfig config_ = nullptr;
  EGLContext context_ = EGL_NO_CONTEXT;
  EGLSurface surface_ = EGL_NO_SURFACE;
  bool resumed_ = true;  // the activity is starting; APP_CMD_PAUSE flips this
  bool paused_event_ = false;  // APP_CMD_PAUSE seen, not yet reported to the game
  bool was_suspended_ = false;
  bool close_requested_ = false;
};

void OnAppCmd(android_app* /*app*/, int32_t cmd) {
  if (g_window) g_window->HandleCommand(cmd);
}

int32_t OnInputEvent(android_app* /*app*/, AInputEvent* event) {
  return g_window ? g_window->HandleInput(event) : 0;
}

}  // namespace

// ---- platform.h ----
bool Init() {
  g_alive = true;  // the EGL display is created with the first window's context
  return true;
}

void Shutdown() {
  if (g_window) {
    g_window->DestroySurface();
    g_window->DestroyContext();
  }
  if (g_display != EGL_NO_DISPLAY) {
    eglTerminate(g_display);
    g_display = EGL_NO_DISPLAY;
  }
  g_alive = false;
}

bool IsAlive() { return g_alive; }

bool HasCurrentGLContext() { return g_alive && eglGetCurrentContext() != EGL_NO_CONTEXT; }

bool LoadGLFunctions() { return true; }  // libGLESv3 is linked directly

std::optional<std::vector<uint8_t>> ReadFile(const std::string& path) {
  // Assets live inside the APK (zip), not on the file system: read them via AAssetManager.
  if (!g_app) return std::nullopt;
  AAsset* asset = AAssetManager_open(g_app->activity->assetManager, path.c_str(), AASSET_MODE_BUFFER);
  if (!asset) return std::nullopt;
  std::vector<uint8_t> data(static_cast<size_t>(AAsset_getLength(asset)));
  const int read = AAsset_read(asset, data.data(), data.size());
  AAsset_close(asset);
  if (read < 0 || static_cast<size_t>(read) != data.size()) return std::nullopt;
  return data;
}

namespace {

// Asks Java for the window insets: NativeActivity has no C API for them, so this goes through
// JNI (Java Native Interface): find the Java classes and methods by name and call them.
// Returns false if they aren't available yet (the view isn't attached to the window).
bool QueryInsets(SafeAreaInsets& out) {
  if (!g_app) return false;
  JavaVM* vm = g_app->activity->vm;
  JNIEnv* env = nullptr;
  bool attached_here = false;
  if (vm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6) == JNI_EDETACHED) {
    if (vm->AttachCurrentThread(&env, nullptr) != JNI_OK) return false;  // our thread -> JVM
    attached_here = true;
  }
  env->PushLocalFrame(32);  // every Java reference created below is freed by PopLocalFrame

  bool ok = false;
  const int sdk = AConfiguration_getSdkVersion(g_app->config);
  // activity.getWindow().getDecorView().getRootWindowInsets()
  jobject activity = g_app->activity->clazz;
  jmethodID get_window = env->GetMethodID(env->GetObjectClass(activity), "getWindow", "()Landroid/view/Window;");
  jobject window = env->CallObjectMethod(activity, get_window);
  jmethodID get_decor = env->GetMethodID(env->GetObjectClass(window), "getDecorView", "()Landroid/view/View;");
  jobject decor = env->CallObjectMethod(window, get_decor);
  jmethodID get_root_insets =
      env->GetMethodID(env->GetObjectClass(decor), "getRootWindowInsets", "()Landroid/view/WindowInsets;");
  jobject insets = env->CallObjectMethod(decor, get_root_insets);
  if (insets && !env->ExceptionCheck()) {
    jclass insets_class = env->GetObjectClass(insets);
    if (sdk >= 30) {
      // Android 11+: insets.getInsets(Type.systemBars() | Type.displayCutout())
      jclass type = env->FindClass("android/view/WindowInsets$Type");
      const jint mask = env->CallStaticIntMethod(type, env->GetStaticMethodID(type, "systemBars", "()I")) |
                        env->CallStaticIntMethod(type, env->GetStaticMethodID(type, "displayCutout", "()I"));
      jobject rect = env->CallObjectMethod(
          insets, env->GetMethodID(insets_class, "getInsets", "(I)Landroid/graphics/Insets;"), mask);
      jclass rect_class = env->GetObjectClass(rect);
      out.left = static_cast<float>(env->GetIntField(rect, env->GetFieldID(rect_class, "left", "I")));
      out.top = static_cast<float>(env->GetIntField(rect, env->GetFieldID(rect_class, "top", "I")));
      out.right = static_cast<float>(env->GetIntField(rect, env->GetFieldID(rect_class, "right", "I")));
      out.bottom = static_cast<float>(env->GetIntField(rect, env->GetFieldID(rect_class, "bottom", "I")));
    } else {
      // Older: the system bars, plus the camera cutout on Android 9-10.
      auto call = [&](jobject object, jclass cls, const char* name) {
        return static_cast<float>(env->CallIntMethod(object, env->GetMethodID(cls, name, "()I")));
      };
      out.left = call(insets, insets_class, "getSystemWindowInsetLeft");
      out.top = call(insets, insets_class, "getSystemWindowInsetTop");
      out.right = call(insets, insets_class, "getSystemWindowInsetRight");
      out.bottom = call(insets, insets_class, "getSystemWindowInsetBottom");
      if (sdk >= 28) {
        jobject cutout = env->CallObjectMethod(
            insets, env->GetMethodID(insets_class, "getDisplayCutout", "()Landroid/view/DisplayCutout;"));
        if (cutout) {
          jclass cutout_class = env->GetObjectClass(cutout);
          out.left = std::max(out.left, call(cutout, cutout_class, "getSafeInsetLeft"));
          out.top = std::max(out.top, call(cutout, cutout_class, "getSafeInsetTop"));
          out.right = std::max(out.right, call(cutout, cutout_class, "getSafeInsetRight"));
          out.bottom = std::max(out.bottom, call(cutout, cutout_class, "getSafeInsetBottom"));
        }
      }
    }
    ok = !env->ExceptionCheck();
  }
  if (env->ExceptionCheck()) env->ExceptionClear();  // a Java exception must not stay pending

  env->PopLocalFrame(nullptr);
  if (attached_here) vm->DetachCurrentThread();
  return ok;
}

}  // namespace

SafeAreaInsets GetSafeAreaInsets() {
  if (!g_insets_valid) {
    SafeAreaInsets insets;
    if (QueryInsets(insets)) {
      g_insets = insets;
      g_insets_valid = true;
    }
  }
  return g_insets;
}

bool MoveToBackground() {
  // activity.moveTaskToBack(true): the same as pressing Home; the app keeps running in the
  // background and comes back exactly where it was.
  if (!g_app) return false;
  JavaVM* vm = g_app->activity->vm;
  JNIEnv* env = nullptr;
  bool attached_here = false;
  if (vm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6) == JNI_EDETACHED) {
    if (vm->AttachCurrentThread(&env, nullptr) != JNI_OK) return false;
    attached_here = true;
  }
  jobject activity = g_app->activity->clazz;
  jclass activity_class = env->GetObjectClass(activity);
  jmethodID move = env->GetMethodID(activity_class, "moveTaskToBack", "(Z)Z");
  const bool moved = move && env->CallBooleanMethod(activity, move, JNI_TRUE);
  if (env->ExceptionCheck()) env->ExceptionClear();
  env->DeleteLocalRef(activity_class);
  if (attached_here) vm->DetachCurrentThread();
  return moved;
}

void WriteLog(LogLevel level, const char* message) {
  const int priority = level == LogLevel::Error   ? ANDROID_LOG_ERROR
                       : level == LogLevel::Warning ? ANDROID_LOG_WARN
                                                    : ANDROID_LOG_INFO;
  __android_log_write(priority, "WLW", message);
}

// ---- entry point (app/entry_point.h) ----
void RunAndroidApp(android_app* app, std::unique_ptr<Application> (*create_app)()) {
  g_app = app;
  app->onAppCmd = OnAppCmd;
  app->onInputEvent = OnInputEvent;

  {
    std::unique_ptr<Application> game = create_app();
    game->Run();
  }  // game, engine and window are destroyed here

  // If the game quit on its own, ask Android to close the activity. The glue requires
  // android_main to keep processing events until the system confirms with destroyRequested.
  if (!app->destroyRequested) {
    ANativeActivity_finish(app->activity);
    while (!app->destroyRequested) PollEvents(-1);
  }
  g_app = nullptr;
}

}  // namespace wlw::platform

namespace wlw::scene {
std::unique_ptr<Window> Window::Create(const core::Vector2& /*size*/, const std::string& /*title*/) {
  return std::make_unique<platform::AndroidWindow>();  // full screen; size comes from the surface
}
}  // namespace wlw::scene

#endif  // WLW_PLATFORM_ANDROID
