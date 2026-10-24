#pragma once

// The platform layer: the only engine code that differs per OS. Everything above it
// (renderer, scene, input, Application, games) is shared.
//
//   platform/glfw/glfw_platform.cpp        Windows, macOS, Linux
//   platform/android/android_platform.cpp  Android (NativeActivity + EGL)
//   (iOS goes here later)
//
// Each implementation also provides scene::Window::Create().

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "wlw/core/log.h"

namespace wlw::platform {

// Called by the renderer before the first window is created / after everything is destroyed.
// Counted: each Init() needs its own Shutdown(), and only the last one shuts the platform down, so
// two renderers can share a process (an app test next to the golden tests' context).
bool Init();
void Shutdown();
bool IsAlive();  // true between the first Init() and the last Shutdown()

// Whether a GL context is current on this thread. GPU-resource destructors check it so that
// objects outliving the engine don't call into a destroyed context.
bool HasCurrentGLContext();

// Resolves GL function pointers once a context is current (glad on desktop; no-op on GLES,
// where libGLESv3 is linked directly).
bool LoadGLFunctions();
// A GL function by name (e.g. an extension's), or null. Needs a current context.
void* GetGLProcAddress(const char* name);
// Whether the current context is OpenGL ES (always on phones; on desktop only for a window made with
// WindowOptions::gles, which the tests use to compile every shader as GLSL ES).
bool CurrentContextIsES();

// Reads a whole asset file. Desktop: a relative path is looked up in the working directory, then
// next to the executable (where the build copies the assets). Android: path inside the APK's
// assets/.
std::optional<std::vector<uint8_t>> ReadFile(const std::string& path);

// ---- Save data: a writable folder private to the app ----
// Android: the app's internal storage (no permission needed; removed on uninstall; included in
// Android's Auto Backup). Windows: %APPDATA%\<app>. macOS: ~/Library/Application Support/<app>.
// Linux: $XDG_DATA_HOME/<app> or ~/.local/share/<app>. Created if missing; "" if impossible.
// Paths are UTF-8.
std::string GetSaveDirectory(const std::string& app_name);
// Writes a file so a crash or power loss can never leave it half-written: the data goes to
// "<path>.tmp", is flushed all the way to the disk, then renamed over <path>. A rename is
// all-or-nothing, so <path> is always either the complete old file or the complete new one.
bool WriteFileAtomic(const std::string& path, const std::vector<uint8_t>& data);
// Reads a file from the device's file system (save data), not from the assets.
std::optional<std::vector<uint8_t>> ReadLocalFile(const std::string& path);

// The parts of the screen covered by system UI or hardware: status bar, camera cutout/notch,
// gesture or navigation bar. Keep important UI inside. In framebuffer pixels; all 0 on desktop.
struct SafeAreaInsets {
  float left = 0.0f;
  float top = 0.0f;
  float right = 0.0f;
  float bottom = 0.0f;
};
SafeAreaInsets GetSafeAreaInsets();

// What Back does when the game has nothing left to close: Android sends the app to the
// background (like the home screen does; the game keeps its state). Returns false where that
// doesn't exist (desktop), and the caller quits instead.
bool MoveToBackground();

// A short vibration ("haptic feedback"): `strength` 0..1 where the device supports amplitude.
// Android needs <uses-permission android:name="android.permission.VIBRATE"/> in the game's
// manifest. No-op on desktop.
void Vibrate(int milliseconds, float strength = 1.0f);

// Keeps the screen from dimming and locking while `on` (like Unity's Screen.sleepTimeout =
// NeverSleep or Unreal's ControlScreensaver): for stretches the player watches without touching,
// such as a replay or a cutscene. Turn it off again when they end, so the phone sleeps as usual.
// Cheap to call every frame (only changes are sent to the OS). Android: the window's
// FLAG_KEEP_SCREEN_ON, no permission. No-op on desktop.
void KeepScreenOn(bool on);

// Waits `seconds` to within ~0.1 ms (the frame limiter, core/frame_pacing.h): the OS sleeps until about
// 1 ms before the end (Windows: a high-resolution waitable timer), then a short spin.
void PreciseSleep(double seconds);

// The program's launch arguments, for cvars ("+r.shadows 0", core/cvar.h): desktop = argv without the
// program's name; Android = the launch intent's "wlw" extra ("r.shadows=0;log.filter=Render=Verbose"),
// one "+name=value" per setting. Stored by the entry point before the game's MakeApp runs.
void SetLaunchArguments(std::vector<std::string> arguments);
const std::vector<std::string>& LaunchArguments();

// The platform's log output (core/log.h's default sink): stdout/stderr with time, thread and category on
// desktop; logcat (tag WLW, "[Category] message") on Android. Called under the log's lock.
void WriteLog(const LogRecord& record);

// A debugger is attached to this process (desktop: Windows, Linux, macOS; Android: Linux's TracerPid).
bool IsDebuggerAttached();
// Stops in the attached debugger here (only call it when one is attached: without one it kills the app).
void BreakIntoDebugger();

}  // namespace wlw::platform
