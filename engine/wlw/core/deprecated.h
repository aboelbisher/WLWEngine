#pragma once

// Deprecation: how an engine API changes without breaking games the same day (like Unreal's UE_DEPRECATED).
//
// Not used today: while nothing published depends on the engine, APIs change directly (CHANGELOG.md: Breaking +
// Migration; CLAUDE.md "Versioning"), and the engine has no shims. This is the tool for when a game ships on a
// tagged engine. Then, when a name changes, the old one stays as a thin shim that forwards to the new one, marked
// with the version that deprecates it (the next release: the minor version after the one in CMakeLists.txt's
// project(VERSION)):
//
//   #if !WLW_NO_DEPRECATED
//   WLW_DEPRECATED(0.3, "Use SetRenderScale()") inline void SetScale(float s) { SetRenderScale(s); }
//   #endif
//
// Games using it get a compiler warning naming the version and the replacement. A shim lives through exactly one
// release: it ships in v0.3.x and is deleted before v0.4.0 is tagged. cmake/CheckDeprecations.cmake (the CTest
// test "deprecations") fails when a shim is older than that, so nothing lingers. The old implementation itself is
// still deleted within its roadmap phase; only the forwarding shim stays.
//
// WLW_NO_DEPRECATED (CMake option, default OFF) compiles every shim out: a game builds with it ON to check it has
// moved off all of them before the next release removes them.

#ifndef WLW_NO_DEPRECATED
#define WLW_NO_DEPRECATED 0
#endif

// `version` is major.minor as a plain number (0.3), `message` a string literal saying what to use instead.
#define WLW_DEPRECATED(version, message) [[deprecated("deprecated in WLWengine " #version ": " message)]]

// Around code that still has to use a deprecated API on purpose (the shims' own tests).
#if defined(_MSC_VER) && !defined(__clang__)
#define WLW_DISABLE_DEPRECATION_WARNINGS_BEGIN __pragma(warning(push)) __pragma(warning(disable : 4996))
#define WLW_DISABLE_DEPRECATION_WARNINGS_END __pragma(warning(pop))
#else
#define WLW_DISABLE_DEPRECATION_WARNINGS_BEGIN \
  _Pragma("GCC diagnostic push") _Pragma("GCC diagnostic ignored \"-Wdeprecated-declarations\"")
#define WLW_DISABLE_DEPRECATION_WARNINGS_END _Pragma("GCC diagnostic pop")
#endif
