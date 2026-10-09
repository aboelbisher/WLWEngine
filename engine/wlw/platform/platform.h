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
bool Init();
void Shutdown();
bool IsAlive();  // true between Init() and Shutdown()

// Whether a GL context is current on this thread. GPU-resource destructors check it so that
// objects outliving the engine don't call into a destroyed context.
bool HasCurrentGLContext();

// Resolves GL function pointers once a context is current (glad on desktop; no-op on GLES,
// where libGLESv3 is linked directly).
bool LoadGLFunctions();

// Reads a whole asset file. Desktop: a relative path is looked up in the working directory, then
// next to the executable (where the build copies the assets). Android: path inside the APK's
// assets/.
std::optional<std::vector<uint8_t>> ReadFile(const std::string& path);

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

void WriteLog(LogLevel level, const char* message);

}  // namespace wlw::platform
