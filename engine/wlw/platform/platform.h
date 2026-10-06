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

// Reads a whole asset file. Desktop: path relative to the working directory (the executable's
// folder). Android: path inside the APK's assets/.
std::optional<std::vector<uint8_t>> ReadFile(const std::string& path);

void WriteLog(LogLevel level, const char* message);

}  // namespace wlw::platform
