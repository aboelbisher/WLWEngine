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
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>

#include <csignal>   // raise(SIGTRAP)
#include <cstdlib>
#include <string>
#include <unistd.h>  // fsync

#include <EGL/egl.h>
#include <GLES3/gl3.h>
#include <jni.h>
#include <android/asset_manager.h>
#include <android/configuration.h>
#include <android/log.h>
#include <android/native_window.h>
#include <android_native_app_glue.h>

#include "wlw/app/application.h"
#include "wlw/platform/bridge.h"
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
    if (share_with) WLW_LOG(LogPlatform, Warning, "Multiple windows aren't supported on Android");
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
    // event loop instead of spinning. The game loop simply stops until we're back - but only after
    // it has seen the pause: one more frame runs first, so the game silences its audio and saves
    // while it still can (Android may end a backgrounded app without another word).
    while (!CanRender() && !paused_event_ && !g_app->destroyRequested && !close_requested_) {
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
      WLW_LOG(LogPlatform, Error, "eglSwapBuffers failed: 0x%x", error);
    }
  }

  bool HasSurface() const override { return surface_ != EGL_NO_SURFACE; }
  bool ShouldClose() const override { return close_requested_ || g_app->destroyRequested; }
  void RequestClose() override { close_requested_ = true; }
  void SetTitle(const std::string&) override {}  // no title bar on Android

  bool ConsumePaused() override {
    const bool paused = paused_event_;
    paused_event_ = false;
    return paused;
  }

  bool ConsumeResumed() override {
    // Not in the frame that reports the pause (the app is still going away), but also when the
    // pause and the resume arrived together (both are reported in that frame, pause first).
    if (!CanRender()) return false;
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
        was_suspended_ = true;  // and must resume once we're back, even if it never had to sleep
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
        WLW_LOG(LogPlatform, Error, "eglInitialize failed: 0x%x", eglGetError());
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
      WLW_LOG(LogPlatform, Error, "No EGL config with OpenGL ES 3 + 24-bit depth");
      return false;
    }
    const EGLint context_attribs[] = {EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE};
    context_ = eglCreateContext(g_display, config_, EGL_NO_CONTEXT, context_attribs);
    if (context_ == EGL_NO_CONTEXT) {
      WLW_LOG(LogPlatform, Error, "eglCreateContext failed: 0x%x", eglGetError());
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
      WLW_LOG(LogPlatform, Error, "eglCreateWindowSurface failed: 0x%x", eglGetError());
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

void* GetGLProcAddress(const char* name) { return reinterpret_cast<void*>(eglGetProcAddress(name)); }

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

void Vibrate(int milliseconds, float strength) {
  if (!g_app || milliseconds <= 0) return;
  JavaVM* vm = g_app->activity->vm;
  JNIEnv* env = nullptr;
  if (vm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6) != JNI_OK) return;  // attached in RunAndroidApp
  env->PushLocalFrame(16);
  // vibrator = activity.getSystemService("vibrator")
  jobject activity = g_app->activity->clazz;
  jmethodID get_service =
      env->GetMethodID(env->GetObjectClass(activity), "getSystemService", "(Ljava/lang/String;)Ljava/lang/Object;");
  jobject vibrator = env->CallObjectMethod(activity, get_service, env->NewStringUTF("vibrator"));
  if (vibrator && !env->ExceptionCheck()) {
    jclass vibrator_class = env->GetObjectClass(vibrator);
    if (AConfiguration_getSdkVersion(g_app->config) >= 26) {
      // Android 8+: VibrationEffect.createOneShot(ms, amplitude 1..255)
      jclass effect_class = env->FindClass("android/os/VibrationEffect");
      jmethodID one_shot = env->GetStaticMethodID(effect_class, "createOneShot", "(JI)Landroid/os/VibrationEffect;");
      const jint amplitude = static_cast<jint>(std::clamp(strength, 0.0f, 1.0f) * 254.0f) + 1;
      jobject effect = env->CallStaticObjectMethod(effect_class, one_shot, static_cast<jlong>(milliseconds), amplitude);
      env->CallVoidMethod(vibrator, env->GetMethodID(vibrator_class, "vibrate", "(Landroid/os/VibrationEffect;)V"),
                          effect);
    } else {
      env->CallVoidMethod(vibrator, env->GetMethodID(vibrator_class, "vibrate", "(J)V"), static_cast<jlong>(milliseconds));
    }
  }
  if (env->ExceptionCheck()) env->ExceptionClear();  // e.g. no VIBRATE permission: just don't vibrate
  env->PopLocalFrame(nullptr);
}

std::string GetSaveDirectory(const std::string& app_name) {
  // NativeActivity gives us the app's private internal storage (/data/data/<package>/files).
  if (!g_app || !g_app->activity->internalDataPath) return {};
  const std::filesystem::path folder = std::filesystem::path(g_app->activity->internalDataPath) / app_name;
  std::error_code error;
  std::filesystem::create_directories(folder, error);
  return std::filesystem::is_directory(folder, error) ? folder.string() : std::string();
}

bool WriteFileAtomic(const std::string& path, const std::vector<uint8_t>& data) {
  const std::string temp = path + ".tmp";
  FILE* file = std::fopen(temp.c_str(), "wb");
  if (!file) return false;
  bool ok = data.empty() || std::fwrite(data.data(), 1, data.size(), file) == data.size();
  ok = ok && std::fflush(file) == 0;     // C library buffer -> kernel
  ok = ok && fsync(fileno(file)) == 0;   // kernel -> flash storage
  std::fclose(file);
  if (!ok || std::rename(temp.c_str(), path.c_str()) != 0) {  // POSIX rename: atomic replace
    std::remove(temp.c_str());
    return false;
  }
  return true;
}

std::optional<std::vector<uint8_t>> ReadLocalFile(const std::string& path) {
  std::ifstream file(path, std::ios::binary);
  if (!file) return std::nullopt;
  return std::vector<uint8_t>(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
}

void WriteLog(const LogRecord& record) {
  int priority = ANDROID_LOG_INFO;
  switch (record.level) {
    case LogLevel::Verbose: priority = ANDROID_LOG_VERBOSE; break;
    case LogLevel::Debug: priority = ANDROID_LOG_DEBUG; break;
    case LogLevel::Info: priority = ANDROID_LOG_INFO; break;
    case LogLevel::Warning: priority = ANDROID_LOG_WARN; break;
    case LogLevel::Error: priority = ANDROID_LOG_ERROR; break;
    case LogLevel::Fatal:
    case LogLevel::Off: priority = ANDROID_LOG_FATAL; break;
  }
  // logcat has its own time and thread columns: only the category goes in front.
  __android_log_print(priority, "WLW", "[%s] %s", record.category, record.message.c_str());
}

// A debugger (ptrace) shows as a non-zero TracerPid in /proc/self/status.
bool IsDebuggerAttached() {
  std::ifstream status("/proc/self/status");
  std::string line;
  while (std::getline(status, line)) {
    if (line.rfind("TracerPid:", 0) == 0) return std::atoi(line.c_str() + 10) != 0;
  }
  return false;
}

void BreakIntoDebugger() { raise(SIGTRAP); }

// ---- The native bridge (bridge.h): C++ <-> com.wlw.WlwBridge (Kotlin) ----
namespace {

jclass g_bridge_class = nullptr;     // global ref; null if the app has no Kotlin side
jmethodID g_bridge_call = nullptr;   // static void call(String service, String method, String json)

std::string ToString(JNIEnv* env, jstring text) {
  if (!text) return {};
  const char* chars = env->GetStringUTFChars(text, nullptr);
  std::string result(chars ? chars : "");
  env->ReleaseStringUTFChars(text, chars);
  return result;
}

// WlwBridge.nativePost(service, event, json): called by Kotlin on any thread.
void JNICALL NativePost(JNIEnv* env, jclass, jstring service, jstring event, jstring json) {
  PostBridgeMessage(ToString(env, service), ToString(env, event), ToString(env, json));
}

// Finds WlwBridge and connects both directions. The class must be looked up through the
// activity's class loader: on a thread we attached ourselves, FindClass only sees system classes.
// And the native method is registered by hand: NativeActivity loads our library without
// System.loadLibrary, so the JVM wouldn't find Java_com_wlw_... symbols by itself.
void ConnectBridge(JNIEnv* env, jobject activity) {
  jobject loader = env->CallObjectMethod(
      activity, env->GetMethodID(env->GetObjectClass(activity), "getClassLoader", "()Ljava/lang/ClassLoader;"));
  jclass loader_class = env->FindClass("java/lang/ClassLoader");
  jmethodID load_class = env->GetMethodID(loader_class, "loadClass", "(Ljava/lang/String;)Ljava/lang/Class;");
  jstring name = env->NewStringUTF("com.wlw.WlwBridge");
  auto bridge = static_cast<jclass>(env->CallObjectMethod(loader, load_class, name));
  env->DeleteLocalRef(name);
  if (env->ExceptionCheck() || !bridge) {
    env->ExceptionClear();
    WLW_LOG(LogPlatform, Info, "No com.wlw.WlwBridge class: the native bridge is off (an app without Kotlin code)");
    return;
  }
  static const JNINativeMethod kNatives[] = {
      {"nativePost", "(Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;)V", reinterpret_cast<void*>(NativePost)},
  };
  if (env->RegisterNatives(bridge, kNatives, 1) != JNI_OK) {
    env->ExceptionClear();
    WLW_LOG(LogPlatform, Error, "Bridge: couldn't register WlwBridge.nativePost");
    return;
  }
  g_bridge_call = env->GetStaticMethodID(bridge, "call", "(Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;)V");
  if (!g_bridge_call) {
    env->ExceptionClear();
    WLW_LOG(LogPlatform, Error, "Bridge: WlwBridge.call(String, String, String) is missing");
    return;
  }
  g_bridge_class = static_cast<jclass>(env->NewGlobalRef(bridge));
}

}  // namespace

bool HasBridge() { return g_bridge_class != nullptr; }

bool BridgeCall(const std::string& service, const std::string& method, const std::string& json_args) {
  if (!g_app || !g_bridge_class) return false;
  JNIEnv* env = nullptr;
  if (g_app->activity->vm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6) != JNI_OK) return false;
  jstring j_service = env->NewStringUTF(service.c_str());
  jstring j_method = env->NewStringUTF(method.c_str());
  jstring j_args = env->NewStringUTF(json_args.c_str());
  env->CallStaticVoidMethod(g_bridge_class, g_bridge_call, j_service, j_method, j_args);
  const bool failed = env->ExceptionCheck();
  if (failed) {
    env->ExceptionDescribe();  // to logcat
    env->ExceptionClear();
  }
  env->DeleteLocalRef(j_service);
  env->DeleteLocalRef(j_method);
  env->DeleteLocalRef(j_args);
  return !failed;
}

// ---- entry point (app/entry_point.h) ----
void RunAndroidApp(android_app* app, std::unique_ptr<::wlw::app::Application> (*create_app)()) {
  g_app = app;
  // Attach this thread (android_main's) to the Java VM once, for all JNI calls (insets, Back,
  // vibration...). Detached again before the thread ends, as the JVM requires.
  JNIEnv* env = nullptr;
  app->activity->vm->AttachCurrentThread(&env, nullptr);
  ConnectBridge(env, app->activity->clazz);
  app->onAppCmd = OnAppCmd;
  app->onInputEvent = OnInputEvent;

  {
    std::unique_ptr<::wlw::app::Application> game = create_app();
    if (game) game->Run();
  }  // game, engine and window are destroyed here

  // If the game quit on its own, ask Android to close the activity. The glue requires
  // android_main to keep processing events until the system confirms with destroyRequested.
  if (!app->destroyRequested) {
    ANativeActivity_finish(app->activity);
    while (!app->destroyRequested) PollEvents(-1);
  }
  if (g_bridge_class) {
    env->DeleteGlobalRef(g_bridge_class);
    g_bridge_class = nullptr;
  }
  app->activity->vm->DetachCurrentThread();
  g_app = nullptr;
}

}  // namespace wlw::platform

namespace wlw::scene {
std::unique_ptr<Window> Window::Create(const core::Vector2& /*size*/, const std::string& /*title*/,
                                       const WindowOptions& /*options*/) {
  return std::make_unique<platform::AndroidWindow>();  // full screen; size comes from the surface
}
}  // namespace wlw::scene

#endif  // WLW_PLATFORM_ANDROID
